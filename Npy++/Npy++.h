#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Enumerators.h>
#include <MemoryMapEnumerators.h>
#include <MemoryMappedFile.h>
#include <StringUtilities.h>

#ifndef _MSC_VER
	#ifdef NDEBUG
		#ifdef __clang__
			#define UNUSED __attribute__((unused))
			#define UNUSED_BUT_SET
			#define UNUSED_ON_NDEBUG(x)
		#else
			#define UNUSED __attribute__((unused))
			#define UNUSED_BUT_SET UNUSED
			#define UNUSED_ON_NDEBUG(x)
		#endif
	#else
		#define UNUSED
		#define UNUSED_BUT_SET
		#define UNUSED_ON_NDEBUG(x) x
	#endif
#else
	#define UNUSED
	#define UNUSED_BUT_SET
	#define UNUSED_ON_NDEBUG(x) x
#endif

namespace npypp
{
	// forward declaration as it's used in detail namespace
	template<typename T>
	struct MultiDimensionalArray;

	namespace detail
	{
		[[nodiscard]] static constexpr char SysEndianness() noexcept
		{
			return (std::endian::native == std::endian::big) ? '>' : '<';
		}

		template<typename T>
		struct Traits
		{
			static constexpr char id { '?' };
		};

		template<typename T>
		static void appendBytes(std::string& out, const T in);	  // in is by value as it's used only for primitive types

		template<typename T>
		[[nodiscard]] static inline T ReadLittleEndian(const unsigned char* src) noexcept
		{
			T val {};
			std::memcpy(&val, src, sizeof(T));
			if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1)
			{
				auto* bytes = reinterpret_cast<unsigned char*>(&val);
				std::reverse(bytes, bytes + sizeof(T));
			}
			return val;
		}

		[[noreturn]] static inline void Fail(const std::string_view msg) noexcept	// LCOV_EXCL_LINE
		{																			// LCOV_EXCL_LINE
			std::cerr << msg << '\n';												// LCOV_EXCL_LINE
			assert(false);															// LCOV_EXCL_LINE
			std::abort();															// LCOV_EXCL_LINE
		}																			// LCOV_EXCL_LINE

		static inline void CheckedSeek(FILE* fp, const long offset, const int whence, const char* errMsg) noexcept
		{
			if (std::fseek(fp, offset, whence) != 0)
				Fail(errMsg);	// LCOV_EXCL_LINE
		}

		static inline void CheckedRead(FILE* fp, void* dst, const size_t elemSize, const size_t count, const char* errMsg) noexcept
		{
			if (count > 0 && std::fread(dst, elemSize, count, fp) != count)
				Fail(errMsg);	// LCOV_EXCL_LINE
		}

		static inline void CheckedWrite(FILE* fp, const void* src, const size_t elemSize, const size_t count, const char* errMsg) noexcept
		{
			if (count > 0 && std::fwrite(src, elemSize, count, fp) != count)
				Fail(errMsg);	// LCOV_EXCL_LINE
		}

		static inline void EnsureZip32Fits(const uint64_t entryBytes, const uint64_t archiveOffset) noexcept
		{
			if (entryBytes > std::numeric_limits<uint32_t>::max() || archiveOffset > std::numeric_limits<uint32_t>::max())
				Fail("Entry or archive exceeds 4 GiB ZIP32 limit");	// LCOV_EXCL_LINE
		}

		template<typename T>
		static inline void ValidateLoadedMetadata(const size_t wordSize, const bool fortranOrder, const std::vector<size_t>& shape) noexcept
		{
			if (wordSize != sizeof(T))
				Fail("Mismatched element word size when loading .npy");	// LCOV_EXCL_LINE
			if (fortranOrder && shape.size() > 1)
				Fail("Fortran-order multi-dimensional arrays are not supported");	// LCOV_EXCL_LINE
		}

		[[nodiscard]] static inline size_t ComputeNumElements(const std::vector<size_t>& shape) noexcept
		{
			size_t nElements = 1;
			for (const size_t dim : shape)
			{
				if (dim != 0 && nElements > std::numeric_limits<size_t>::max() / dim)
					Fail("Array element count overflows size_t");	// LCOV_EXCL_LINE
				nElements *= dim;
			}
			return nElements;
		}

#pragma region Npy Utilities

		template<typename T>
		static std::string GetNpyHeaderDescription();

		static inline std::string GetNpyHeaderFortranOrder() { return "'fortran_order': False"; }

		static inline std::string GetNpyHeaderShape(const std::vector<size_t>& shape)
		{
			std::string ret;
			ret.reserve(12 + shape.size() * 8);

			ret += "'shape': (";
			for (const auto i : shape)
			{
				ret += std::to_string(i);
				ret += ", ";
			}
			ret += ")";

			return ret;
		}

		[[nodiscard]] static inline std::string PadNpyHeader(std::string properties, const size_t preambleBytes = 10)
		{
			// Pad with spaces so (preambleBytes + dictSize) % 16 == 0, ending with '\n'.
			// preambleBytes is 10 for NPY v1.0 (8 magic + 2 header_len) and 12 for v2.0 (8 magic + 4 header_len).
			constexpr size_t moduloBytes = 16;
			const size_t totalBeforePad = preambleBytes + properties.size() + 1;
			const size_t rem = totalBeforePad % moduloBytes;
			const size_t paddingSpaces = (rem == 0) ? 0 : (moduloBytes - rem);

			properties.reserve(properties.size() + paddingSpaces + 1);
			properties.append(paddingSpaces, ' ');
			properties.push_back('\n');

			return properties;
		}

		static inline auto GetMagic(const uint8_t major = 1)
		{
			return std::string("\x93NUMPY", 6) + static_cast<char>(major) + '\x00';
		}

		template<typename T>
		static std::string GetNpyHeader(const std::vector<size_t>& shape);

		[[nodiscard]] static inline bool ParseFortranOrder(const std::string_view npyHeader) noexcept
		{
			const size_t keyPos = npyHeader.find("fortran_order");
			if (keyPos == std::string_view::npos)
				Fail("Malformed .npy header: missing 'fortran_order'");	// LCOV_EXCL_LINE

			const size_t colonPos = npyHeader.find(':', keyPos);
			if (colonPos == std::string_view::npos)
				Fail("Malformed .npy header: missing ':' after 'fortran_order'");	// LCOV_EXCL_LINE

			const size_t valStart = npyHeader.find_first_not_of(" \t", colonPos + 1);
			if (valStart == std::string_view::npos)
				Fail("Malformed .npy header: missing 'fortran_order' value");	// LCOV_EXCL_LINE

			return npyHeader.substr(valStart, 4) == "True";
		}

		[[nodiscard]] static inline std::vector<size_t> ParseShape(const std::string_view npyHeader) noexcept
		{
			const size_t keyPos = npyHeader.find("shape");
			const size_t searchStart = (keyPos != std::string_view::npos) ? keyPos : 0;
			const size_t loc1 = npyHeader.find('(', searchStart);
			const size_t loc2 = npyHeader.find(')', loc1 == std::string_view::npos ? searchStart : loc1 + 1);
			if (loc1 == std::string_view::npos || loc2 == std::string_view::npos || loc2 <= loc1)
				Fail("Malformed .npy header: invalid 'shape' tuple");	// LCOV_EXCL_LINE

			const std::string_view tupleView = npyHeader.substr(loc1 + 1, loc2 - loc1 - 1);
			std::vector<size_t> shape;

			size_t pos = 0;
			while (pos < tupleView.size())
			{
				const size_t digitStart = tupleView.find_first_not_of(" \t,", pos);
				if (digitStart == std::string_view::npos)
					break;

				size_t dim = 0;
				const char* begin = tupleView.data() + digitStart;
				const char* end = tupleView.data() + tupleView.size();
				const auto [ptr, ec] = std::from_chars(begin, end, dim);
				if (ec != std::errc{})
					Fail("Malformed .npy header: invalid dimension in 'shape'");	// LCOV_EXCL_LINE

				shape.push_back(dim);
				pos = static_cast<size_t>(ptr - tupleView.data());
			}

			return shape;
		}

		static inline void ParseDescription(const std::string_view npyHeader, size_t& wordSize, char& endianness) noexcept
		{
			const size_t keyPos = npyHeader.find("descr");
			if (keyPos == std::string_view::npos)
				Fail("Malformed .npy header: missing 'descr'");	// LCOV_EXCL_LINE

			const size_t colonPos = npyHeader.find(':', keyPos);
			if (colonPos == std::string_view::npos)
				Fail("Malformed .npy header: missing ':' after 'descr'");	// LCOV_EXCL_LINE

			const size_t quotePos = npyHeader.find_first_of("'\"", colonPos + 1);
			if (quotePos == std::string_view::npos)
				Fail("Malformed .npy header: missing quote in 'descr'");	// LCOV_EXCL_LINE

			const char quoteChar = npyHeader[quotePos];
			const size_t closeQuotePos = npyHeader.find(quoteChar, quotePos + 1);
			if (closeQuotePos == std::string_view::npos || closeQuotePos < quotePos + 3)
				Fail("Malformed .npy header: invalid 'descr' string");	// LCOV_EXCL_LINE

			const std::string_view descr = npyHeader.substr(quotePos + 1, closeQuotePos - quotePos - 1);
			char parsedEndian = descr[0];
			if (parsedEndian == '=')
				parsedEndian = SysEndianness();
			if (parsedEndian != '<' && parsedEndian != '|' && parsedEndian != '>')
				Fail("Malformed .npy header: invalid endianness marker in 'descr'");	// LCOV_EXCL_LINE
			endianness = parsedEndian;

			size_t parsedWordSize = 0;
			const auto [ptr, ec] = std::from_chars(descr.data() + 2, descr.data() + descr.size(), parsedWordSize);
			if (ec != std::errc{} || ptr != descr.data() + descr.size())
				Fail("Malformed .npy header: invalid word size in 'descr'");	// LCOV_EXCL_LINE
			wordSize = parsedWordSize;
		}

		static inline void ParseNpyHeader(const std::string_view header, size_t& wordSize, std::vector<size_t>& shape, bool& fortranOrder, char& endianness) noexcept
		{
			if (header.empty() || header.back() != '\n')
				Fail("Malformed .npy header: missing trailing newline");	// LCOV_EXCL_LINE
			fortranOrder = ParseFortranOrder(header);
			shape = ParseShape(header);
			ParseDescription(header, wordSize, endianness);
		}

		[[nodiscard]] static inline size_t ParseNpyPreamble(const unsigned char* preamble10, uint8_t& majorVersion, uint32_t& headerLen) noexcept
		{
			static constexpr std::array<unsigned char, 6> magic { 0x93, 'N', 'U', 'M', 'P', 'Y' };
			if (std::memcmp(preamble10, magic.data(), magic.size()) != 0)
				Fail("Invalid .npy file: bad magic header");	// LCOV_EXCL_LINE

			majorVersion = preamble10[6];
			if (majorVersion == 1)
			{
				headerLen = ReadLittleEndian<uint16_t>(preamble10 + 8);
				return 10;
			}
			if (majorVersion != 2 && majorVersion != 3)
				Fail("Unsupported .npy format version");	// LCOV_EXCL_LINE

			headerLen = ReadLittleEndian<uint32_t>(preamble10 + 8);
			return 12;
		}

		static inline size_t ParseNpyHeader(const std::span<const unsigned char> buffer, size_t& wordSize, std::vector<size_t>& shape, bool& fortranOrder, char& endianness) noexcept
		{
			if (buffer.size() < 10)
				Fail("Truncated .npy buffer");	// LCOV_EXCL_LINE

			const uint8_t majorVersion = buffer[6];
			const size_t preambleSize = (majorVersion == 1) ? 10 : 12;
			if (buffer.size() < preambleSize)
				Fail("Truncated .npy preamble");	// LCOV_EXCL_LINE

			uint8_t parsedMajor = 0;
			uint32_t headerLen = 0;
			std::array<unsigned char, 12> preamble {};
			std::memcpy(preamble.data(), buffer.data(), preambleSize);
			std::ignore = ParseNpyPreamble(preamble.data(), parsedMajor, headerLen);

			const size_t totalHeaderSize = preambleSize + static_cast<size_t>(headerLen);
			if (buffer.size() < totalHeaderSize)
				Fail("Truncated .npy header dictionary");	// LCOV_EXCL_LINE

			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			const std::string_view headerView(reinterpret_cast<const char*>(buffer.data() + preambleSize), headerLen);
			ParseNpyHeader(headerView, wordSize, shape, fortranOrder, endianness);
			return totalHeaderSize;
		}

		static inline size_t ParseNpyHeader(FILE* fp, size_t& wordSize, std::vector<size_t>& shape, bool& fortranOrder, char& endianness) noexcept
		{
			if (fp == nullptr)
				Fail("Cannot read .npy header from null FILE*");	// LCOV_EXCL_LINE

			std::array<unsigned char, 12> preamble {};
			CheckedRead(fp, preamble.data(), sizeof(unsigned char), 10, "Failed to read .npy preamble");

			const uint8_t majorVersion = preamble[6];
			size_t preambleSize = 10;
			if (majorVersion == 2 || majorVersion == 3)
			{
				CheckedRead(fp, preamble.data() + 10, sizeof(unsigned char), 2, "Failed to read extended .npy header length");
				preambleSize = 12;
			}

			uint8_t parsedMajor = 0;
			uint32_t headerLen = 0;
			std::ignore = ParseNpyPreamble(preamble.data(), parsedMajor, headerLen);

			std::string header(headerLen, '\0');
			CheckedRead(fp, header.data(), sizeof(char), headerLen, "Failed to read .npy header dictionary");

			ParseNpyHeader(std::string_view(header), wordSize, shape, fortranOrder, endianness);
			return preambleSize + static_cast<size_t>(headerLen);
		}

		template<typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
		static size_t ParseNpyHeader(mm::MemoryMappedFile<ch, mpm>& mmf, size_t& wordSize, std::vector<size_t>& shape, bool& fortranOrder, char& endianness) noexcept;

		template<typename T>
		static MultiDimensionalArray<T> LoadFull(FILE* fp);

		template<typename T, typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
		static MultiDimensionalArray<T> LoadFull(mm::MemoryMappedFile<ch, mpm>& mmf);

		template<typename T, typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
		static std::span<const T> LoadSpan(mm::MemoryMappedFile<ch, mpm>& mmf, std::vector<size_t>* outShape = nullptr);

		template<typename T, typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
		static void LoadNoCopy(const T*& data, mm::MemoryMappedFile<ch, mpm>& mmf);

#pragma endregion

#pragma region Npz Utilities

		static inline void ParseNpzFooter(FILE* fp, uint16_t& nRecords, size_t& globalHeaderSize, size_t& globalHeaderOffset) noexcept
		{
			if (fp == nullptr)
				Fail("Cannot read .npz footer from null FILE*");	// LCOV_EXCL_LINE

			CheckedSeek(fp, 0, SEEK_END, "Failed seeking to end of .npz file");

			const long fileSizeLong = std::ftell(fp);
			if (fileSizeLong < 22)
				Fail("Truncated .npz file: smaller than EOCD record");	// LCOV_EXCL_LINE

			const auto fileSize = static_cast<size_t>(fileSizeLong);
			const size_t searchLen = std::min(fileSize, static_cast<size_t>(22 + 65535));
			CheckedSeek(fp, -static_cast<long>(searchLen), SEEK_END, "Failed seeking to EOCD search window in .npz file");

			std::vector<unsigned char> tail(searchLen);
			CheckedRead(fp, tail.data(), 1, searchLen, "Failed reading EOCD search window in .npz file");

			size_t eocdOffset = std::string::npos;
			for (size_t step = searchLen - 21; step > 0; --step)
			{
				const size_t i = step - 1;
				if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06)
				{
					const uint16_t commentLen = ReadLittleEndian<uint16_t>(&tail[i + 20]);
					if (i + 22 + commentLen == searchLen)
					{
						eocdOffset = i;
						break;
					}
				}
			}

			if (eocdOffset == std::string::npos)
				Fail("Invalid .npz file: EOCD signature not found");	// LCOV_EXCL_LINE

			const unsigned char* footer = &tail[eocdOffset];
			nRecords = ReadLittleEndian<uint16_t>(footer + 10);
			globalHeaderSize = ReadLittleEndian<uint32_t>(footer + 12);
			globalHeaderOffset = ReadLittleEndian<uint32_t>(footer + 16);

#ifndef NDEBUG
			assert(ReadLittleEndian<uint16_t>(footer + 4) == 0);
			assert(ReadLittleEndian<uint16_t>(footer + 6) == 0);
			assert(ReadLittleEndian<uint16_t>(footer + 8) == nRecords);
#endif
		}

		template<typename T>
		static uint32_t GetCrcNpyFile(const std::string& npyHeader, const std::vector<T>& data);

		static inline std::string GetLocalHeader(const uint32_t crc, const uint32_t nBytes, const std::string& localNpyFileName)
		{
			std::string ret;
			ret.reserve(30 + localNpyFileName.size());
			ret += "PK";						   // signature magic
			appendBytes<uint16_t>(ret, 0x0403);	   // signature magic

			appendBytes<uint16_t>(ret, 20);	   // min version to extract
			appendBytes<uint16_t>(ret, 0);	   // general purpose bit flag
			appendBytes<uint16_t>(ret, 0);	   // compression method
			appendBytes<uint16_t>(ret, 0);	   // file last mod time
			appendBytes<uint16_t>(ret, 0);	   // file last mod date

			appendBytes<uint32_t>(ret, crc);
			appendBytes<uint32_t>(ret, nBytes);	   // compressed size
			appendBytes<uint32_t>(ret, nBytes);	   // uncompressed size

			appendBytes<uint16_t>(ret, static_cast<uint16_t>(localNpyFileName.size()));	   // npy file size
			appendBytes<uint16_t>(ret, 0);												   // extra field length

			ret += localNpyFileName;

			return ret;
		}

		static inline std::string MakeGlobalHeaderEntry(const std::string& localHeader, const uint32_t globalHeaderOffset, const std::string& localNpyFileName)
		{
			std::string entry;
			entry.reserve(46 + localNpyFileName.size());
			entry += "PK";						   // signature magic
			appendBytes<uint16_t>(entry, 0x0201);  // signature magic
			appendBytes<uint16_t>(entry, 20);	   // version made by

			entry.append(localHeader.data() + 4, 26);

			appendBytes<uint16_t>(entry, 0);					   // file comment length
			appendBytes<uint16_t>(entry, 0);					   // disk number where file starts
			appendBytes<uint16_t>(entry, 0);					   // internal file attributes
			appendBytes<uint32_t>(entry, 0);					   // external file attributes
			appendBytes<uint32_t>(entry, globalHeaderOffset);	   // relative offset of local file header

			entry += localNpyFileName;
			return entry;
		}

		static inline std::string GetNpzFooter(const std::string& globalHeader, const uint32_t globalHeaderOffset, const uint32_t localHeaderSize, const uint32_t nElementsInBytes, const uint16_t nNewRecords)
		{
			std::string ret;
			ret.reserve(22);
			ret += "PK";						   // signature magic
			appendBytes<uint16_t>(ret, 0x0605);	   // signature magic

			appendBytes<uint16_t>(ret, 0);	  // number of this disk
			appendBytes<uint16_t>(ret, 0);	  // disk where footer starts

			appendBytes<uint16_t>(ret, nNewRecords);	// number of records on this disk
			appendBytes<uint16_t>(ret, nNewRecords);	// total number of records

			appendBytes<uint32_t>(ret, static_cast<uint32_t>(globalHeader.size()));					// nbytes of global headers
			appendBytes<uint32_t>(ret, globalHeaderOffset + nElementsInBytes + localHeaderSize);	// offset of start of global headers

			appendBytes<uint16_t>(ret, 0);	  // zip file comment length

			return ret;
		}

		template<typename T>
		MultiDimensionalArray<T> LoadCompressedFull(FILE* fp, uint32_t compressedBytes, uint32_t uncompressedBytes);

		template<typename T>
		std::unordered_map<std::string, MultiDimensionalArray<T>> LoadCompressedMap(const std::string& zipFileName, std::optional<std::string_view> targetName = std::nullopt);

#pragma endregion
	}	 // namespace detail

#pragma region Convenience Types

	template<typename T>
	struct MultiDimensionalArray
	{
		MultiDimensionalArray(std::vector<T> data_, std::vector<size_t> shape_) noexcept : data(std::move(data_)), shape(std::move(shape_)) {}

		MultiDimensionalArray() = default;
		~MultiDimensionalArray() = default;
		MultiDimensionalArray(const MultiDimensionalArray&) = default;
		MultiDimensionalArray(MultiDimensionalArray&&) noexcept = default;
		MultiDimensionalArray& operator=(const MultiDimensionalArray&) = default;
		MultiDimensionalArray& operator=(MultiDimensionalArray&&) noexcept = default;

		std::vector<T> data {};
		std::vector<size_t> shape {};
	};

	template<typename T>
	struct Vector: public MultiDimensionalArray<T>
	{
		explicit Vector(std::vector<T> data_) : MultiDimensionalArray<T>(std::move(data_), {}) { this->shape = { this->data.size() }; }

		using MultiDimensionalArray<T>::MultiDimensionalArray;
	};

	template<typename T>
	struct Matrix: public MultiDimensionalArray<T>
	{
		Matrix(std::vector<T> data_, const size_t nRows, const size_t nCols) : MultiDimensionalArray<T>(std::move(data_), { nRows, nCols }) {}

		using MultiDimensionalArray<T>::MultiDimensionalArray;
	};

	template<typename T>
	struct Tensor: public MultiDimensionalArray<T>
	{
		Tensor(std::vector<T> data_, const size_t nDim0, const size_t nDim1, const size_t nDim2) : MultiDimensionalArray<T>(std::move(data_), { nDim0, nDim1, nDim2 }) {}

		using MultiDimensionalArray<T>::MultiDimensionalArray;
	};

#pragma endregion

#pragma region Load / Save Npy

	// Generic API
	/**
	 * If appending, it will increase the number of rows (i.e. the first dimension in the shape vector)
	 * for more flexibility use the Append method
	 */
	template<typename T>
	void Save(const std::string& fileName, const std::vector<T>& data, const std::vector<size_t>& shape, const std::string& mode = "w");

	template<typename T, typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
	void Save(mm::MemoryMappedFile<ch, mpm>& mmf, const std::vector<T>& data, const std::vector<size_t>& shape);

	template<typename T>
	void Save(const std::string& fileName, const std::vector<T>& data, const std::vector<size_t>& shape, const FileOpenMode mode = FileOpenMode::Write)
	{
		Save(fileName, data, shape, ToString(mode));
	}

	template<typename T>
	void Save(const std::string& fileName, const MultiDimensionalArray<T>& array, const FileOpenMode mode = FileOpenMode::Write)
	{
		Save(fileName, array.data, array.shape, ToString(mode));
	}

	/**
	 * Load the full info (data and shape) from the file
	 */
	template<typename T>
	MultiDimensionalArray<T> LoadFull(const std::string& fileName, const bool useMemoryMap = false);

	/**
	 * Load the full info (data and shape) from the file using an externally set memory mapped file
	 */
	template<typename T, typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
	MultiDimensionalArray<T> LoadFull(mm::MemoryMappedFile<ch, mpm>& mmf);

	template<typename T>
	std::vector<T> Load(const std::string& fileName, const bool useMemoryMap = false)
	{
		return LoadFull<T>(fileName, useMemoryMap).data;
	}

	template<typename T, typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
	std::vector<T> Load(mm::MemoryMappedFile<ch, mpm>& mmf)
	{
		return LoadFull<T>(mmf).data;
	}

	/**
	 * Zero-copy read-only span view directly into a memory-mapped .npy file.
	 * Requires host-compatible endianness, matching element size, C-order, and natural alignment.
	 */
	template<typename T, typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
	std::span<const T> LoadSpan(mm::MemoryMappedFile<ch, mpm>& mmf, std::vector<size_t>* outShape = nullptr)
	{
		return detail::LoadSpan<T, ch, mpm>(mmf, outShape);
	}

	template<typename T, typename mm::CacheHint ch = mm::CacheHint::SequentialScan, typename mm::MapMode mpm = mm::MapMode::ReadOnly>
	void Load(const T*& data, mm::MemoryMappedFile<ch, mpm>& mmf)
	{
		detail::LoadNoCopy<T, ch, mpm>(data, mmf);
	}

	// API with convenience types
	template<typename T>
	void Save(const std::string& fileName, const MultiDimensionalArray<T>& array, const std::string& mode = "w")
	{
		Save(fileName, array.data, array.shape, mode);
	}

#pragma endregion

	template<typename T>
	using CompressedMap = std::unordered_map<std::string, std::vector<T>>;

	template<typename T>
	using CompressedMapFull = std::unordered_map<std::string, MultiDimensionalArray<T>>;

#pragma region Load / Save Npz

	/**
	 * Since *.npz files supports multiple arrays in a single file, the variable name needs to be specified
	 */
	template<typename T>
	void SaveCompressed(const std::string& zipFileName, std::string vectorName, const std::vector<T>& data, const std::vector<size_t>& shape, const std::string& mode = "w");

	/**
	 * If vector name is not provided, it's extracted by the zipfile name
	 */
	template<typename T>
	void SaveCompressed(const std::string& zipFileName, const std::vector<T>& data, const std::vector<size_t>& shape, const std::string& mode = "w")
	{
		std::string vectorName = std::filesystem::path(zipFileName).stem().string();
		SaveCompressed(zipFileName, std::move(vectorName), data, shape, mode);
	}

	/**
	 * Limitations: map has only one value type, so you cannot load different types in the same file
	 */
	template<typename T>
	CompressedMapFull<T> LoadCompressedFull(const std::string& zipFileName);

	/**
	 * Load a single variable from an *.npz archive without decompressing unrelated entries.
	 */
	template<typename T>
	MultiDimensionalArray<T> LoadCompressedFull(const std::string& zipFileName, const std::string& vectorName)
	{
		CompressedMapFull<T> fullInfo = detail::LoadCompressedMap<T>(zipFileName, std::string_view(vectorName));

		auto iter = fullInfo.find(vectorName);
		if (iter == fullInfo.end())
			return MultiDimensionalArray<T>();

		return std::move(iter->second);
	}

	/**
	 * Limitations: map has only one value type, so you cannot load different types in the same file
	 */
	template<typename T>
	CompressedMap<T> LoadCompressed(const std::string& zipFileName)
	{
		CompressedMapFull<T> fullInfo = LoadCompressedFull<T>(zipFileName);
		CompressedMap<T> ret;
		ret.reserve(fullInfo.size());

		for (auto& iter : fullInfo)
			ret[iter.first] = std::move(iter.second.data);

		return ret;
	}

	/**
	 * Limitations: map has only one value type, so you cannot load different types in the same file
	 */
	template<typename T>
	std::vector<T> LoadCompressed(const std::string& zipFileName, const std::string& vectorName)
	{
		return LoadCompressedFull<T>(zipFileName, vectorName).data;
	}

#pragma endregion
}	 // namespace npypp

#include <Npy++.tpp>
