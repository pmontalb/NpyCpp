#pragma once

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

// OS-specific
#ifdef _WIN32
	// Windows
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
#else
	// Linux / POSIX
	// enable large file support on 32-bit systems
	#ifndef _LARGEFILE64_SOURCE
		#define _LARGEFILE64_SOURCE
	#endif

	#ifdef _FILE_OFFSET_BITS
		#undef _FILE_OFFSET_BITS
	#endif

	#define _FILE_OFFSET_BITS 64

	#include <cerrno>
	#include <fcntl.h>
	#include <sys/mman.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

namespace mm
{
	/// open file
	template<CacheHint ch, MapMode mpm>
	bool MemoryMappedFile<ch, mpm>::Open() noexcept
	{
		// already open ?
		if (IsValid())
			return false;

		_fileHandle = InvalidFileHandle;
		_fileSize = 0;
		_mappedBytes = 0;

#ifdef _WIN32
		_mappedFile = nullptr;
#endif
		_mappedView = nullptr;
		_originMappedView = nullptr;

#ifdef _WIN32
		DWORD winHint = 0;
		switch (ch)
		{
			case CacheHint::Normal:
				winHint = FILE_ATTRIBUTE_NORMAL;
				break;
			case CacheHint::SequentialScan:
				winHint = FILE_FLAG_SEQUENTIAL_SCAN;
				break;
			case CacheHint::RandomAccess:
				winHint = FILE_FLAG_RANDOM_ACCESS;
				break;
			default:
				break;
		}

		// open file
		switch (mpm)
		{
			case MapMode::ReadOnly:
				_fileHandle = ::CreateFileA(_filename.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, winHint, nullptr);
				break;
			case MapMode::WriteOnly:
			case MapMode::ReadAndWrite:
				_fileHandle = ::CreateFileA(_filename.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, winHint, nullptr);
				break;
			default:
				break;
		}
		if (_fileHandle == INVALID_HANDLE_VALUE || _fileHandle == nullptr)
		{
			_fileHandle = InvalidFileHandle;
			return false;
		}

		// file size
		LARGE_INTEGER result {};
		if (!::GetFileSizeEx(_fileHandle, &result))
		{
			::CloseHandle(_fileHandle);
			_fileHandle = InvalidFileHandle;
			return false;
		}
		_fileSize = static_cast<std::uint64_t>(result.QuadPart);

		if constexpr (mpm != MapMode::ReadOnly)
		{
			if (_initialMappedBytes > 0 && _fileSize < _initialMappedBytes)
			{
				LARGE_INTEGER targetSize {};
				targetSize.QuadPart = static_cast<LONGLONG>(_initialMappedBytes);
				if (!::SetFilePointerEx(_fileHandle, targetSize, nullptr, FILE_BEGIN) || !::SetEndOfFile(_fileHandle))
				{
					::CloseHandle(_fileHandle);
					_fileHandle = InvalidFileHandle;
					return false;
				}
				_fileSize = _initialMappedBytes;
			}
		}

		// convert to mapped mode
		switch (mpm)
		{
			case MapMode::ReadOnly:
				_mappedFile = ::CreateFileMapping(_fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
				break;
			case MapMode::WriteOnly:
			case MapMode::ReadAndWrite:
				_mappedFile = ::CreateFileMapping(_fileHandle, nullptr, PAGE_READWRITE, 0, 0, nullptr);
				break;
			default:
				break;
		}
		if (!_mappedFile)
		{
			::CloseHandle(_fileHandle);
			_fileHandle = InvalidFileHandle;
			return false;
		}
#else
		// Linux / POSIX
		switch (mpm)
		{
			case MapMode::ReadOnly:
				// NOLINTNEXTLINE(*)
				_fileHandle = ::open(_filename.c_str(), O_RDONLY | O_LARGEFILE);
				break;
			case MapMode::WriteOnly:
			case MapMode::ReadAndWrite:
				// NOLINTNEXTLINE(*)
				_fileHandle = ::open(_filename.c_str(), O_RDWR | O_CREAT | O_LARGEFILE, 0644);
				break;
			default:
				_fileHandle = InvalidFileHandle;
				break;
		}
		if (_fileHandle == InvalidFileHandle)
			return false;

		// file size
		struct stat64 statInfo
		{
		};
		bool ok = (::fstat64(_fileHandle, &statInfo) == 0);
		if (ok)
		{
			_fileSize = static_cast<std::uint64_t>(statInfo.st_size);
			if constexpr (mpm != MapMode::ReadOnly)
			{
				if (_initialMappedBytes > 0 && _fileSize < _initialMappedBytes)
				{
					ok = (::ftruncate64(_fileHandle, static_cast<off64_t>(_initialMappedBytes)) == 0);
					if (ok)
						_fileSize = _initialMappedBytes;
				}
			}
		}
		if (!ok)
		{
			Close();
			return false;
		}
#endif

		// initial mapping
		if (!ReMap(0, _initialMappedBytes))
		{
			Close();
			return false;
		}

		return true;
	}

	/// close file
	template<CacheHint ch, MapMode mpm>
	void MemoryMappedFile<ch, mpm>::Close() noexcept
	{
		if (_originMappedView != nullptr)
		{
#ifdef _WIN32
			::UnmapViewOfFile(_originMappedView);
#else
			::munmap(_originMappedView, _mappedBytes);
#endif
			_mappedView = nullptr;
			_originMappedView = nullptr;
			_mappedBytes = 0;
		}

#ifdef _WIN32
		if (_mappedFile != nullptr)
		{
			::CloseHandle(_mappedFile);
			_mappedFile = nullptr;
		}
#endif

		// close underlying file
		if (_fileHandle != InvalidFileHandle)
		{
#ifdef _WIN32
			::CloseHandle(_fileHandle);
#else
			::close(_fileHandle);
#endif
			_fileHandle = InvalidFileHandle;
		}

		_fileSize = 0;
	}

	/// replace mapping by a new one of the same file, offset MUST be a multiple of the page size
	template<CacheHint ch, MapMode mpm>
	bool MemoryMappedFile<ch, mpm>::ReMap(std::uint64_t offset, std::size_t mappedBytes) noexcept
	{
		if (_fileHandle == InvalidFileHandle)
			return false;

		// close old mapping using its actual mapped size
		if (_originMappedView != nullptr)
		{
#ifdef _WIN32
			::UnmapViewOfFile(_originMappedView);
#else
			::munmap(_originMappedView, _mappedBytes);
#endif
			_mappedView = nullptr;
			_originMappedView = nullptr;
			_mappedBytes = 0;
		}

		if (offset >= _fileSize)
			return false;

		const auto remainingFileBytes = _fileSize - offset;
		auto bytesToMap = (mappedBytes == 0) ? static_cast<std::size_t>(remainingFileBytes) : mappedBytes;
		if (static_cast<std::uint64_t>(bytesToMap) > remainingFileBytes)
			bytesToMap = static_cast<std::size_t>(remainingFileBytes);

#ifdef _WIN32
		const DWORD offsetLow = static_cast<DWORD>(offset & 0xFFFFFFFFULL);
		const DWORD offsetHigh = static_cast<DWORD>(offset >> 32U);

		switch (mpm)
		{
			case MapMode::ReadOnly:
				_mappedView = ::MapViewOfFile(_mappedFile, FILE_MAP_READ, offsetHigh, offsetLow, bytesToMap);
				break;
			case MapMode::WriteOnly:
				_mappedView = ::MapViewOfFile(_mappedFile, FILE_MAP_WRITE, offsetHigh, offsetLow, bytesToMap);
				break;
			case MapMode::ReadAndWrite:
				_mappedView = ::MapViewOfFile(_mappedFile, FILE_MAP_ALL_ACCESS, offsetHigh, offsetLow, bytesToMap);
				break;
			default:
				break;
		}

		if (!_mappedView)
		{
			_mappedBytes = 0;
			_originMappedView = nullptr;
			return false;
		}

		_originMappedView = _mappedView;
		_mappedBytes = bytesToMap;
		return true;
#else
		int prot = 0;
		switch (mpm)
		{
			case MapMode::ReadOnly:
				prot = PROT_READ;
				break;
			case MapMode::WriteOnly:
				prot = PROT_WRITE;
				break;
			case MapMode::ReadAndWrite:
				prot = PROT_READ | PROT_WRITE;
				break;
			default:				// LCOV_EXCL_LINE
				return false;		// LCOV_EXCL_LINE
		}

		// NOLINTNEXTLINE(*)
		void* mapped = ::mmap(nullptr, bytesToMap, prot, MAP_SHARED, _fileHandle, static_cast<off_t>(offset));
		// NOLINTNEXTLINE(*)
		if (mapped == MAP_FAILED)
		{
			perror("mmap");
			return false;
		}

		_mappedView = mapped;
		_originMappedView = mapped;
		_mappedBytes = bytesToMap;

		int linuxHint = MADV_NORMAL;
		switch (ch)
		{
			case CacheHint::Normal:
				linuxHint = MADV_NORMAL;
				break;
			case CacheHint::SequentialScan:
				linuxHint = MADV_SEQUENTIAL;
				break;
			case CacheHint::RandomAccess:
				linuxHint = MADV_RANDOM;
				break;
			default:
				break;
		}

		::madvise(_originMappedView, _mappedBytes, linuxHint);

		return true;
#endif
	}

	template<CacheHint ch, MapMode mpm>
	std::string MemoryMappedFile<ch, mpm>::ReadLine(const std::size_t maxCharToRead) noexcept
	{
		std::string out;
		const auto remaining = RemainingBytes();
		const auto limit = std::min(maxCharToRead, remaining);
		if (limit > 0)
		{
			const auto* buffer = GetData();
			const void* nl = std::memchr(buffer, '\n', limit);
			if (nl != nullptr)
			{
				const auto len = static_cast<std::size_t>(static_cast<const unsigned char*>(nl) - buffer);
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				out.assign(reinterpret_cast<const char*>(buffer), len + 1);
				Advance(len);
			}
			else
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				out.assign(reinterpret_cast<const char*>(buffer), limit);
				Advance(limit);
			}
		}

		return out;
	}

	template<CacheHint ch, MapMode mpm>
	template<typename T>
	[[maybe_unused]] void MemoryMappedFile<ch, mpm>::CopyTo(T* data, const std::size_t nElementsToRead) noexcept
	{
		const auto bytesToCopy = sizeof(T) * nElementsToRead;
		if (bytesToCopy == 0)
			return;
		assert(bytesToCopy <= RemainingBytes());
		const auto* buffer = GetData();
		std::memcpy(data, buffer, bytesToCopy);
	}

	template<CacheHint ch, MapMode mpm>
	template<typename T>
	void MemoryMappedFile<ch, mpm>::Set(const T*& data) noexcept
	{
		data = static_cast<const T*>(_mappedView);
	}

	template<CacheHint ch, MapMode mpm>
	void MemoryMappedFile<ch, mpm>::ReadFrom(unsigned const char* data, const std::size_t nElementsToWrite) noexcept
	{
		if (nElementsToWrite == 0)
			return;
		assert(nElementsToWrite <= RemainingBytes());
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		auto* buffer = reinterpret_cast<unsigned char*>(_mappedView);
		std::memcpy(buffer, data, nElementsToWrite);

		Advance(nElementsToWrite);
	}
}	 // namespace mm
