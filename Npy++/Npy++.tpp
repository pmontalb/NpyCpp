#pragma once

#include <complex>
#include <cstdio>
#include <limits>
#include <type_traits>

#include <StringUtilities.h>
#include <zlib.h>

#ifdef _WIN32
	#define FOPEN(FILE_POINTER, FILE_NAME, MODE) fopen_s(&FILE_POINTER, FILE_NAME, MODE)
#else
	#define FOPEN(FILE_POINTER, FILE_NAME, MODE) FILE_POINTER = fopen(FILE_NAME, MODE)
#endif

namespace npypp
{
	namespace detail
	{
#pragma region Type Traits

#define MAKE_TYPE_TRAITS(TYPE, CHAR)                                                                                                                                                                   \
	template<>                                                                                                                                                                                         \
	struct Traits<TYPE>                                                                                                                                                                                \
	{                                                                                                                                                                                                  \
		static constexpr char id { CHAR };                                                                                                                                                             \
	}

		MAKE_TYPE_TRAITS(float, 'f');
		MAKE_TYPE_TRAITS(double, 'f');
		MAKE_TYPE_TRAITS(long double, 'f');

		MAKE_TYPE_TRAITS(short, 'i');
		MAKE_TYPE_TRAITS(char, 'i');
		MAKE_TYPE_TRAITS(int, 'i');
		MAKE_TYPE_TRAITS(long, 'i');
		MAKE_TYPE_TRAITS(long long, 'i');

		MAKE_TYPE_TRAITS(unsigned short, 'u');
		MAKE_TYPE_TRAITS(unsigned char, 'u');
		MAKE_TYPE_TRAITS(unsigned int, 'u');
		MAKE_TYPE_TRAITS(unsigned long, 'u');
		MAKE_TYPE_TRAITS(unsigned long long, 'u');

		MAKE_TYPE_TRAITS(bool, 'b');

		template<typename T>
		struct Traits<std::complex<T>>
		{
			static constexpr char id { 'c' };
		};

#undef MAKE_TYPE_TRAITS

		template<typename T>
		struct is_complex: std::false_type
		{
		};

		template<typename T>
		struct is_complex<std::complex<T>>: std::true_type
		{
		};

		template<typename T>
		struct scalar_or_complex_elem
		{
			using type = T;
		};

		template<typename T>
		struct scalar_or_complex_elem<std::complex<T>>
		{
			using type = T;
		};

#pragma endregion

		template<typename T>
		void appendBytes(std::string& out, T in)
		{
			if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				auto* bytes = reinterpret_cast<unsigned char*>(&in);
				std::reverse(bytes, bytes + sizeof(T));
			}
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			out.append(reinterpret_cast<const char*>(&in), sizeof(T));
		}

#pragma region Npy Header

		template<typename T>
		[[maybe_unused]] std::string GetNpyHeaderDescription()
		{
			std::string ret;
			ret.reserve(24);
			ret += "'descr': '";
			ret += SysEndianness();
			ret += Traits<T>::id;
			ret += std::to_string(sizeof(T));
			ret += "'";

			return ret;
		}

		template<typename T>
		[[maybe_unused]] std::string GetNpyHeader(const std::vector<std::size_t>& shape)
		{
			std::string properties;
			properties.reserve(80);
			properties += "{";
			properties += GetNpyHeaderDescription<T>();
			properties += ", ";
			properties += GetNpyHeaderFortranOrder();
			properties += ", ";
			properties += GetNpyHeaderShape(shape);
			properties += ", }";

			const auto useV2 = (properties.size() + 1 > std::numeric_limits<std::uint16_t>::max());
			const auto majorVersion = static_cast<std::uint8_t>(useV2 ? 2 : 1);
			const auto preambleBytes = static_cast<std::size_t>(useV2 ? 12 : 10);
			const auto padded = PadNpyHeader(std::move(properties), preambleBytes);

			std::string header;
			header.reserve(preambleBytes + padded.size());
			header += GetMagic(majorVersion);
			if (useV2)
				appendBytes<std::uint32_t>(header, static_cast<std::uint32_t>(padded.size()));
			else
				appendBytes<std::uint16_t>(header, static_cast<std::uint16_t>(padded.size()));
			header += padded;

			return header;
		}

		template<typename mm::CacheHint ch, typename mm::MapMode mpm>
		[[maybe_unused]] std::size_t ParseNpyHeader(mm::MemoryMappedFile<ch, mpm>& mmf, std::size_t& wordSize, std::vector<std::size_t>& shape, bool& fortranOrder, char& endianness) noexcept
		{
			if (mmf.RemainingBytes() < 10)
				Fail("Truncated memory-mapped .npy file");	// LCOV_EXCL_LINE

			const auto* preamble = mmf.GetData();
			const auto majorVersion = preamble[6];
			const auto preambleSize = static_cast<std::size_t>((majorVersion == 1) ? 10 : 12);
			if (mmf.RemainingBytes() < preambleSize)
				Fail("Truncated memory-mapped .npy preamble");	// LCOV_EXCL_LINE

			std::uint8_t parsedMajor = 0;
			std::uint32_t headerLen = 0;
			std::ignore = ParseNpyPreamble(preamble, parsedMajor, headerLen);
			mmf.Advance(preambleSize);

			if (mmf.RemainingBytes() < headerLen)
				Fail("Truncated memory-mapped .npy header");	// LCOV_EXCL_LINE

			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			const std::string_view headerView(reinterpret_cast<const char*>(mmf.GetData()), headerLen);
			ParseNpyHeader(headerView, wordSize, shape, fortranOrder, endianness);
			mmf.Advance(headerLen);

			return preambleSize + static_cast<std::size_t>(headerLen);
		}

#pragma endregion

		template<typename ElemT>
		static inline void SwapScalarBytes(ElemT* buffer, const std::size_t count) noexcept
		{
			if constexpr (sizeof(ElemT) == 1)
			{
				static_cast<void>(buffer);
				static_cast<void>(count);
			}
			else if constexpr (sizeof(ElemT) == 2)
			{
				for (std::size_t i = 0; i < count; ++i)
				{
					std::uint16_t v = 0;
					std::memcpy(&v, buffer + i, sizeof(v));
					const auto uv = static_cast<unsigned int>(v);
					v = static_cast<std::uint16_t>(((uv >> 8U) | (uv << 8U)) & 0xFFFFU);
					std::memcpy(buffer + i, &v, sizeof(v));
				}
			}
			else if constexpr (sizeof(ElemT) == 4)
			{
				for (std::size_t i = 0; i < count; ++i)
				{
					std::uint32_t v = 0;
					std::memcpy(&v, buffer + i, sizeof(v));
					v = ((v & 0x000000FFU) << 24U) | ((v & 0x0000FF00U) << 8U) | ((v & 0x00FF0000U) >> 8U) | ((v & 0xFF000000U) >> 24U);
					std::memcpy(buffer + i, &v, sizeof(v));
				}
			}
			else if constexpr (sizeof(ElemT) == 8)
			{
				for (std::size_t i = 0; i < count; ++i)
				{
					std::uint64_t v = 0;
					std::memcpy(&v, buffer + i, sizeof(v));
					v = ((v & 0x00000000000000FFULL) << 56U) | ((v & 0x000000000000FF00ULL) << 40U) | ((v & 0x0000000000FF0000ULL) << 24U) |
						((v & 0x00000000FF000000ULL) << 8U) | ((v & 0x000000FF00000000ULL) >> 8U) | ((v & 0x0000FF0000000000ULL) >> 24U) |
						((v & 0x00FF000000000000ULL) >> 40U) | ((v & 0xFF00000000000000ULL) >> 56U);
					std::memcpy(buffer + i, &v, sizeof(v));
				}
			}
			else
			{
				for (std::size_t i = 0; i < count; ++i)
				{
					// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
					auto* bytes = reinterpret_cast<std::uint8_t*>(buffer + i);
					for (std::size_t b = 0; b < sizeof(ElemT) / 2; ++b)
						std::swap(bytes[b], bytes[sizeof(ElemT) - 1 - b]);
				}
			}
		}

		template<typename T>
		[[maybe_unused]] static void SwapEndianness(T* buffer, std::size_t size) noexcept
		{
			if constexpr (is_complex<T>::value)
			{
				using ScalarT = typename scalar_or_complex_elem<T>::type;
				// std::complex<U> is guaranteed layout-compatible with U[2] ([complex.numbers.general]/4)
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				SwapScalarBytes(reinterpret_cast<ScalarT*>(buffer), size * 2);
			}
			else
			{
				SwapScalarBytes(buffer, size);
			}
		}

		template<typename T>
		[[maybe_unused]] static void SwapEndianness(std::vector<T>& v) noexcept
		{
			SwapEndianness(v.data(), v.size());
		}

		template<typename T>
		[[maybe_unused]] MultiDimensionalArray<T> LoadFull(std::FILE* fp)
		{
			if (fp == nullptr)
				Fail("Cannot load .npy from null FILE*");	// LCOV_EXCL_LINE

			std::vector<std::size_t> shape;
			std::size_t wordSize = 0;
			bool fortranOrder = false;
			char endianness = 0;
			std::ignore = detail::ParseNpyHeader(fp, wordSize, shape, fortranOrder, endianness);
			ValidateLoadedMetadata<T>(wordSize, fortranOrder, shape);

			const auto nElements = ComputeNumElements(shape);
			std::vector<T> data(nElements);

			CheckedRead(fp, data.data(), sizeof(T), nElements, "Truncated .npy payload");

			if (endianness != '|' && (endianness != SysEndianness()))
				SwapEndianness(data);

			return MultiDimensionalArray<T>(std::move(data), std::move(shape));
		}

		template<typename T, typename mm::CacheHint ch, typename mm::MapMode mpm>
		[[maybe_unused]] MultiDimensionalArray<T> LoadFull(mm::MemoryMappedFile<ch, mpm>& mmf)
		{
			static_assert(mpm != mm::MapMode::WriteOnly, "Cannot load from WriteOnly memory-mapped file");
			if (!mmf.IsValid())
				Fail("MemoryMappedFile is not valid");	// LCOV_EXCL_LINE

			std::vector<std::size_t> shape;
			std::size_t wordSize = 0;
			bool fortranOrder = false;
			char endianness = 0;
			std::ignore = detail::ParseNpyHeader(mmf, wordSize, shape, fortranOrder, endianness);
			ValidateLoadedMetadata<T>(wordSize, fortranOrder, shape);

			const auto nElements = ComputeNumElements(shape);
			if (nElements * sizeof(T) > mmf.RemainingBytes())
				Fail("Truncated memory-mapped .npy payload");	// LCOV_EXCL_LINE

			std::vector<T> data(nElements);
			mmf.CopyTo(data);

			if (endianness != '|' && (endianness != SysEndianness()))
				SwapEndianness(data);

			return MultiDimensionalArray<T>(std::move(data), std::move(shape));
		}

		template<typename T, typename mm::CacheHint ch, typename mm::MapMode mpm>
		[[maybe_unused]] std::span<const T> LoadSpan(mm::MemoryMappedFile<ch, mpm>& mmf, std::vector<std::size_t>* outShape)
		{
			static_assert(mpm != mm::MapMode::WriteOnly, "Cannot zero-copy load from WriteOnly memory-mapped file");
			if (!mmf.IsValid())
				Fail("MemoryMappedFile is not valid");	// LCOV_EXCL_LINE

			std::vector<std::size_t> shape;
			std::size_t wordSize = 0;
			bool fortranOrder = false;
			char endianness = 0;
			std::ignore = detail::ParseNpyHeader(mmf, wordSize, shape, fortranOrder, endianness);
			ValidateLoadedMetadata<T>(wordSize, fortranOrder, shape);
			if (endianness != '|' && endianness != SysEndianness())
				Fail("Non-native endianness is not supported for zero-copy .npy span");	// LCOV_EXCL_LINE

			const auto nElements = ComputeNumElements(shape);
			if (nElements * sizeof(T) > mmf.RemainingBytes())
				Fail("Truncated memory-mapped .npy payload");	// LCOV_EXCL_LINE

			const T* ptr = nullptr;
			mmf.Set(ptr);
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			if (reinterpret_cast<std::uintptr_t>(ptr) % alignof(T) != 0)
				Fail("Mapped .npy payload does not meet target type alignment");	// LCOV_EXCL_LINE

			if (outShape != nullptr)
				*outShape = std::move(shape);

			return std::span<const T>(ptr, nElements);
		}

		/**
		 * Zero-copy pointer overload into a memory-mapped .npy file.
		 */
		template<typename T, typename mm::CacheHint ch, typename mm::MapMode mpm>
		[[maybe_unused]] void LoadNoCopy(const T*& data, mm::MemoryMappedFile<ch, mpm>& mmf)
		{
			data = LoadSpan<T, ch, mpm>(mmf).data();
		}

#pragma region Npz Utilities

		template<typename T>
		[[maybe_unused]] std::uint32_t GetCrcNpyFile(const std::string& npyHeader, const std::vector<T>& data)
		{
			constexpr std::size_t maxChunk = std::numeric_limits<uInt>::max();
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			auto crc = crc32(0L, reinterpret_cast<const std::uint8_t*>(npyHeader.data()), static_cast<uInt>(npyHeader.size()));

			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			const auto* ptr = reinterpret_cast<const std::uint8_t*>(data.data());
			auto remaining = data.size() * sizeof(T);
			while (remaining > 0)
			{
				const auto chunk = static_cast<uInt>(std::min(remaining, maxChunk));
				crc = crc32(crc, ptr, chunk);
				ptr += chunk;
				remaining -= chunk;
			}
			return static_cast<std::uint32_t>(crc);
		}

		struct InflateStreamGuard
		{
			z_stream& stream;
			bool initialized = false;
			~InflateStreamGuard()
			{
				if (initialized)
					inflateEnd(&stream);
			}
		};

		static inline void InflateRaw(unsigned char* src, const std::uint32_t compressedBytes, unsigned char* dst, const std::uint32_t uncompressedBytes, const int windowBits = -MAX_WBITS) noexcept
		{
			z_stream stream {};
			InflateStreamGuard guard { stream, false };
			if (inflateInit2(&stream, windowBits) != Z_OK)
				Fail("zlib inflateInit2 failed");	// LCOV_EXCL_LINE
			guard.initialized = true;

			stream.avail_in = compressedBytes;
			stream.next_in = src;
			stream.avail_out = uncompressedBytes;
			stream.next_out = dst;

			const auto rc = inflate(&stream, Z_FINISH);
			if (rc != Z_STREAM_END || stream.total_out != uncompressedBytes)
				Fail("zlib inflate failed or decompressed size mismatch");	// LCOV_EXCL_LINE
		}

		template<typename T>
		MultiDimensionalArray<T> LoadCompressedFull(std::FILE* fp, std::uint32_t compressedBytes, std::uint32_t uncompressedBytes)
		{
			std::vector<unsigned char> bufferCompressed(compressedBytes);
			CheckedRead(fp, bufferCompressed.data(), 1, compressedBytes, "Truncated compressed .npz entry");

			std::vector<unsigned char> bufferUncompressed(uncompressedBytes);
			InflateRaw(bufferCompressed.data(), compressedBytes, bufferUncompressed.data(), uncompressedBytes);

			std::vector<std::size_t> shape;
			std::size_t wordSize = 0;
			bool fortranOrder = false;
			char endianness = 0;
			const auto headerBytes = detail::ParseNpyHeader(std::span<const unsigned char>(bufferUncompressed), wordSize, shape, fortranOrder, endianness);
			ValidateLoadedMetadata<T>(wordSize, fortranOrder, shape);

			const auto nElements = ComputeNumElements(shape);
			const auto nElementsInBytes = nElements * sizeof(T);
			if (headerBytes + nElementsInBytes > uncompressedBytes)
				Fail("Decompressed .npy payload is smaller than declared shape");	// LCOV_EXCL_LINE

			std::vector<T> data(nElements);
			if (nElementsInBytes > 0)
				std::memcpy(data.data(), bufferUncompressed.data() + headerBytes, nElementsInBytes);

			if (endianness != '|' && (endianness != SysEndianness()))
				SwapEndianness(data);

			return MultiDimensionalArray<T>(std::move(data), std::move(shape));
		}

		template<typename T>
		std::unordered_map<std::string, MultiDimensionalArray<T>> LoadCompressedMap(const std::string& zipFileName, std::optional<std::string_view> targetName)
		{
			std::FILE* fp = nullptr;
			FOPEN(fp, zipFileName.c_str(), "rb");
			if (fp == nullptr)
				Fail("Failed to open .npz file");	// LCOV_EXCL_LINE

			struct FileCloser
			{
				std::FILE* f;
				~FileCloser()
				{
					if (f != nullptr)
						std::fclose(f);
				}
			} closer { fp };

			std::uint16_t nRecords = 0;
			std::size_t globalHeaderSize = 0;
			std::size_t globalHeaderOffset = 0;
			detail::ParseNpzFooter(fp, nRecords, globalHeaderSize, globalHeaderOffset);

			CheckedSeek(fp, static_cast<std::int64_t>(globalHeaderOffset), SEEK_SET, "Failed seeking to ZIP central directory");

			std::vector<unsigned char> centralDir(globalHeaderSize);
			CheckedRead(fp, centralDir.data(), 1, globalHeaderSize, "Failed reading ZIP central directory");

			std::unordered_map<std::string, MultiDimensionalArray<T>> ret;
			if (!targetName.has_value())
				ret.reserve(nRecords);

			std::size_t cdCursor = 0;
			for (std::uint16_t rec = 0; rec < nRecords && cdCursor + 46 <= centralDir.size(); ++rec)
			{
				const auto* cdh = &centralDir[cdCursor];
				if (cdh[0] != 0x50 || cdh[1] != 0x4b || cdh[2] != 0x01 || cdh[3] != 0x02)
					Fail("Invalid ZIP central directory header");	// LCOV_EXCL_LINE

				const auto compressionMethod = ReadLittleEndian<std::uint16_t>(cdh + 10);
				const auto compressedBytes = ReadLittleEndian<std::uint32_t>(cdh + 20);
				const auto uncompressedBytes = ReadLittleEndian<std::uint32_t>(cdh + 24);
				const auto nameLen = ReadLittleEndian<std::uint16_t>(cdh + 28);
				const auto extraLen = ReadLittleEndian<std::uint16_t>(cdh + 30);
				const auto commentLen = ReadLittleEndian<std::uint16_t>(cdh + 32);
				const auto localHeaderOffset = ReadLittleEndian<std::uint32_t>(cdh + 42);

				if (cdCursor + 46 + nameLen + extraLen + commentLen > centralDir.size())
					Fail("Truncated ZIP central directory entry");	// LCOV_EXCL_LINE

				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				std::string vectorName(reinterpret_cast<const char*>(cdh + 46), nameLen);
				if (vectorName.ends_with(".npy"))
					vectorName.resize(vectorName.size() - 4);

				cdCursor += 46 + static_cast<std::size_t>(nameLen) + extraLen + commentLen;

				if (targetName.has_value() && vectorName != *targetName)
					continue;

				CheckedSeek(fp, static_cast<std::int64_t>(localHeaderOffset), SEEK_SET, "Failed seeking to ZIP local header");

				std::array<unsigned char, 30> localHeader {};
				CheckedRead(fp, localHeader.data(), 1, localHeader.size(), "Failed reading ZIP local header");

				if (localHeader[0] != 0x50 || localHeader[1] != 0x4b || localHeader[2] != 0x03 || localHeader[3] != 0x04)
					Fail("Invalid ZIP local header signature");	// LCOV_EXCL_LINE

				const auto localNameLen = ReadLittleEndian<std::uint16_t>(localHeader.data() + 26);
				const auto localExtraLen = ReadLittleEndian<std::uint16_t>(localHeader.data() + 28);
				CheckedSeek(fp, static_cast<std::int64_t>(localNameLen) + static_cast<std::int64_t>(localExtraLen), SEEK_CUR, "Failed skipping ZIP local header metadata");

				if (compressionMethod == 0)
					ret.emplace(std::move(vectorName), detail::LoadFull<T>(fp));
				else if (compressionMethod != 8)
					Fail("Unsupported ZIP compression method in .npz");	// LCOV_EXCL_LINE
				else
					ret.emplace(std::move(vectorName), detail::LoadCompressedFull<T>(fp, compressedBytes, uncompressedBytes));

				if (targetName.has_value())
					break;
			}

			return ret;
		}

#pragma endregion
	}	 // namespace detail

#pragma region Load / Save Npy

	template<typename T>
	void Save(const std::string& fileName, const std::vector<T>& data, const std::vector<std::size_t>& shape, const std::string& mode)
	{
		std::FILE* fp = nullptr;
		std::vector<std::size_t> actualShape;
		std::size_t oldHeaderSize = 0;
		bool appending = false;

		if (mode == "a")
			FOPEN(fp, fileName.c_str(), "r+b");

		if (fp != nullptr)
			appending = true;
		else
			FOPEN(fp, fileName.c_str(), "wb");

		if (fp == nullptr)
			detail::Fail("Failed to open .npy file for writing");	// LCOV_EXCL_LINE

		struct FileCloser
		{
			std::FILE* f;
			~FileCloser()
			{
				if (f != nullptr)
					std::fclose(f);
			}
		} closer { fp };

		if (appending)
		{
			std::size_t wordSize = 0;
			bool fortranOrder = false;
			char endianness = 0;
			oldHeaderSize = detail::ParseNpyHeader(fp, wordSize, actualShape, fortranOrder, endianness);
			if (fortranOrder)
				detail::Fail("Cannot append to Fortran-order .npy file");	// LCOV_EXCL_LINE
			if (wordSize != sizeof(T))
				detail::Fail("Cannot append mismatched element word size to .npy file");	// LCOV_EXCL_LINE
			if (actualShape.size() != shape.size() || actualShape.empty())
				detail::Fail("Cannot append mismatched rank to .npy file");	// LCOV_EXCL_LINE

			for (std::size_t i = 1; i < shape.size(); ++i)
			{
				if (shape[i] != actualShape[i])
					detail::Fail("Cannot append mismatched non-leading dimensions to .npy file");	// LCOV_EXCL_LINE
			}
			actualShape[0] += shape[0];
		}
		else
		{
			actualShape = shape;
		}

		const auto header = detail::GetNpyHeader<T>(actualShape);
		const auto nElements = detail::ComputeNumElements(shape);

		if (appending && header.size() > oldHeaderSize)
		{
			// Shift existing payload forward so the enlarged header never overwrites array data.
			const auto shift = header.size() - oldHeaderSize;
			detail::CheckedSeek(fp, 0, SEEK_END, "Failed seeking to end of .npy file");
			const auto fileEnd = static_cast<std::size_t>(detail::CheckedTell(fp, "Failed determining .npy file size"));
			if (fileEnd > oldHeaderSize)
			{
				auto remaining = fileEnd - oldHeaderSize;
				constexpr std::size_t chunkSize = 65536;
				std::vector<char> copyBuf(std::min(remaining, chunkSize));
				while (remaining > 0)
				{
					const auto toMove = std::min(remaining, copyBuf.size());
					const auto srcOffset = oldHeaderSize + remaining - toMove;
					detail::CheckedSeek(fp, static_cast<std::int64_t>(srcOffset), SEEK_SET, "Failed seeking .npy payload during append");
					detail::CheckedRead(fp, copyBuf.data(), 1, toMove, "Failed reading .npy payload during append");
					detail::CheckedSeek(fp, static_cast<std::int64_t>(srcOffset + shift), SEEK_SET, "Failed seeking .npy shifted payload during append");
					detail::CheckedWrite(fp, copyBuf.data(), 1, toMove, "Failed writing .npy shifted payload during append");
					remaining -= toMove;
				}
			}
		}

		detail::CheckedSeek(fp, 0, SEEK_SET, "Failed seeking to start of .npy file");
		detail::CheckedWrite(fp, header.data(), sizeof(char), header.size(), "Failed writing .npy header");
		detail::CheckedSeek(fp, 0, SEEK_END, "Failed seeking to end of .npy file before data write");
		detail::CheckedWrite(fp, data.data(), sizeof(T), nElements, "Failed writing .npy payload");
	}

	template<typename T, typename mm::CacheHint ch, typename mm::MapMode mpm>
	void Save(mm::MemoryMappedFile<ch, mpm>& mmf, const std::vector<T>& data, const std::vector<std::size_t>& shape)
	{
		if (!mmf.IsValid())
			detail::Fail("MemoryMappedFile is not valid");	// LCOV_EXCL_LINE

		const auto header = detail::GetNpyHeader<T>(shape);
		const auto nElements = detail::ComputeNumElements(shape);
		if (header.size() + sizeof(T) * nElements > mmf.RemainingBytes())
			detail::Fail("MemoryMappedFile mapping is smaller than .npy header + payload");	// LCOV_EXCL_LINE

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		const auto* headerBuffer = reinterpret_cast<const unsigned char*>(header.data());
		mmf.ReadFrom(headerBuffer, header.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		const auto* dataBuffer = reinterpret_cast<const unsigned char*>(data.data());
		mmf.ReadFrom(dataBuffer, sizeof(T) * nElements);
	}

	/**
	 * Load the full info (data and shape) from the file
	 */
	template<typename T>
	MultiDimensionalArray<T> LoadFull(const std::string& fileName, const bool useMemoryMap)
	{
		if (!useMemoryMap)
		{
			std::FILE* fp = nullptr;
			FOPEN(fp, fileName.c_str(), "rb");
			if (fp == nullptr)
				detail::Fail("Failed to open .npy file");	// LCOV_EXCL_LINE

			struct FileCloser
			{
				std::FILE* f;
				~FileCloser()
				{
					if (f != nullptr)
						std::fclose(f);
				}
			} closer { fp };

			return detail::LoadFull<T>(fp);
		}

		mm::MemoryMappedFile<mm::CacheHint::SequentialScan, mm::MapMode::ReadOnly> mmf(fileName);
		if (!mmf.IsValid())
			detail::Fail("Failed to memory-map .npy file");	// LCOV_EXCL_LINE
		return detail::LoadFull<T, mm::CacheHint::SequentialScan>(mmf);
	}

	/**
	 * Load the full info (data and shape) from the file using an externally set memory mapped file
	 */
	template<typename T, typename mm::CacheHint ch, typename mm::MapMode mpm>
	MultiDimensionalArray<T> LoadFull(mm::MemoryMappedFile<ch, mpm>& mmf)
	{
		static_assert(mpm != mm::MapMode::WriteOnly, "Cannot load from WriteOnly memory-mapped file");
		return detail::LoadFull<T, ch, mpm>(mmf);
	}

#pragma endregion

#pragma region Load / Save Npz

	template<typename T>
	void SaveCompressed(const std::string& zipFileName, std::string vectorName, const std::vector<T>& data, const std::vector<std::size_t>& shape, const std::string& mode)
	{
		vectorName += ".npy";

		std::FILE* fp = nullptr;
		std::uint16_t nRecords = 0;
		std::size_t globalHeaderOffset = 0;
		std::string globalHeader;
		bool appending = false;

		if (mode == "a")
			FOPEN(fp, zipFileName.c_str(), "r+b");

		if (fp != nullptr)
			appending = true;
		else
			FOPEN(fp, zipFileName.c_str(), "wb");

		if (fp == nullptr)
			detail::Fail("Failed to open .npz file for writing");	// LCOV_EXCL_LINE

		struct FileCloser
		{
			std::FILE* f;
			~FileCloser()
			{
				if (f != nullptr)
					std::fclose(f);
			}
		} closer { fp };

		if (appending)
		{
			std::size_t globalHeaderSize = 0;
			detail::ParseNpzFooter(fp, nRecords, globalHeaderSize, globalHeaderOffset);
			if (nRecords == std::numeric_limits<std::uint16_t>::max())
				detail::Fail("ZIP32 .npz archive record limit reached");	// LCOV_EXCL_LINE
			globalHeader.resize(globalHeaderSize);
			detail::CheckedSeek(fp, static_cast<std::int64_t>(globalHeaderOffset), SEEK_SET, "Failed seeking to ZIP central directory");
			detail::CheckedRead(fp, globalHeader.data(), sizeof(char), globalHeaderSize, "Failed reading ZIP central directory");
			detail::CheckedSeek(fp, static_cast<std::int64_t>(globalHeaderOffset), SEEK_SET, "Failed seeking to write position in .npz");
		}

		const auto npyHeader = detail::GetNpyHeader<T>(shape);
		const auto nElements = detail::ComputeNumElements(shape);
		const auto nElementsInBytes = nElements * sizeof(T);
		const auto totalNpyFileBytes = nElementsInBytes + npyHeader.size();

		detail::EnsureZip32Fits(totalNpyFileBytes, globalHeaderOffset);

		const auto crc = detail::GetCrcNpyFile(npyHeader, data);
		const auto localHeader = detail::GetLocalHeader(crc, static_cast<std::uint32_t>(totalNpyFileBytes), vectorName);
		globalHeader += detail::MakeGlobalHeaderEntry(localHeader, static_cast<std::uint32_t>(globalHeaderOffset), vectorName);
		const auto footer = detail::GetNpzFooter(globalHeader, static_cast<std::uint32_t>(globalHeaderOffset), static_cast<std::uint32_t>(localHeader.size()),
												 static_cast<std::uint32_t>(totalNpyFileBytes), static_cast<std::uint16_t>(nRecords + 1));

		detail::CheckedWrite(fp, localHeader.data(), sizeof(char), localHeader.size(), "Failed writing .npz local header");
		detail::CheckedWrite(fp, npyHeader.data(), sizeof(char), npyHeader.size(), "Failed writing .npz entry header");
		detail::CheckedWrite(fp, data.data(), sizeof(T), nElements, "Failed writing .npz entry payload");
		detail::CheckedWrite(fp, globalHeader.data(), sizeof(char), globalHeader.size(), "Failed writing .npz central directory");
		detail::CheckedWrite(fp, footer.data(), sizeof(char), footer.size(), "Failed writing .npz footer");
	}

	template<typename T>
	CompressedMapFull<T> LoadCompressedFull(const std::string& zipFileName)
	{
		return detail::LoadCompressedMap<T>(zipFileName, std::nullopt);
	}

#pragma endregion
}	 // namespace npypp
