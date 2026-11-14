#include "pch.h"
#include <Npy++.h>

#include <complex>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <numeric>
#include <span>

namespace
{
	void ReverseBytes(const std::span<uint8_t> bytes) noexcept
	{
		for (size_t i = 0; i < bytes.size() / 2; ++i)
			std::swap(bytes[i], bytes[bytes.size() - 1 - i]);
	}
}	 // namespace

constexpr size_t Nx { 128 };
constexpr size_t Ny { 64 };
constexpr size_t Nz { 32 };
constexpr size_t TotalSize { Nx * Ny * Nz };
const std::vector<size_t> shape { Nz, Ny, Nx };

class MmapNpyTests: public ::testing::Test
{
public:
	MmapNpyTests() : data(Nx * Ny * Nz) {}

	void SetUp() override
	{
		for (size_t i = 0; i < Nx * Ny * Nz; i++)
			data[i] = std::complex<double>(rand(), rand());
	}

	void TearDown() override
	{
		std::error_code ec;
		std::filesystem::remove("arr1.npy", ec);
		std::filesystem::remove("fresh_mmap_write.npy", ec);
		std::filesystem::remove("big_endian_mmap.npy", ec);
		std::filesystem::remove("aligned_span.npy", ec);
		std::filesystem::remove("empty_file.npy", ec);
		std::filesystem::remove("be_types.npy", ec);
		std::filesystem::remove("small_rw.npy", ec);
		std::filesystem::remove("read_line.npy", ec);
		std::filesystem::remove("v2_mmap.npy", ec);
	}

protected:
	std::vector<std::complex<double>> data;
};

TEST_F(MmapNpyTests, SaveAndReadFromMappedFile)
{
	npypp::Save("arr1.npy", data, shape, "w");

	auto loadedData = npypp::Load<std::complex<double>>("arr1.npy", true);

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData[i]);
}

TEST_F(MmapNpyTests, SaveFromMappedFileAndRead)
{
	// need to specify the file size beforehand
	const auto nElements = std::accumulate(shape.begin(), shape.end(), std::size_t { 1 }, std::multiplies<>());
	const auto npyHeader = npypp::detail::GetNpyHeader<std::complex<double>>(shape);
	const size_t fileSizeBytes = npyHeader.size() + nElements * sizeof(std::complex<double>);
	auto mmf = std::make_unique<mm::MemoryMappedFile<mm::CacheHint::SequentialScan, mm::MapMode::WriteOnly>>("arr1.npy", fileSizeBytes);
	ASSERT_TRUE(mmf->IsValid());
	npypp::Save(*mmf, data, shape);

	auto loadedData = npypp::Load<std::complex<double>>("arr1.npy");

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData[i]);
}

TEST_F(MmapNpyTests, SaveFromMappedFileAndReadFull)
{
	// need to specify the file size beforehand
	const auto nElements = std::accumulate(shape.begin(), shape.end(), std::size_t { 1 }, std::multiplies<>());
	const auto npyHeader = npypp::detail::GetNpyHeader<std::complex<double>>(shape);
	const size_t fileSizeBytes = npyHeader.size() + nElements * sizeof(std::complex<double>);
	auto mmf = std::make_unique<mm::MemoryMappedFile<mm::CacheHint::SequentialScan, mm::MapMode::WriteOnly>>("arr1.npy", fileSizeBytes);
	ASSERT_TRUE(mmf->IsValid());
	npypp::Save(*mmf, data, shape);

	auto loadedData = npypp::LoadFull<std::complex<double>>("arr1.npy");
	ASSERT_EQ(loadedData.shape.size(), 3);
	ASSERT_EQ(loadedData.shape[0], Nz);
	ASSERT_EQ(loadedData.shape[1], Ny);
	ASSERT_EQ(loadedData.shape[2], Nx);
	ASSERT_EQ(loadedData.data.size(), TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData.data[i]);
}

TEST_F(MmapNpyTests, SaveFromMappedFileAndReadFromMappedFile)
{
	// need to specify the file size beforehand
	const auto nElements = std::accumulate(shape.begin(), shape.end(), std::size_t { 1 }, std::multiplies<>());
	const auto npyHeader = npypp::detail::GetNpyHeader<std::complex<double>>(shape);
	const size_t fileSizeBytes = npyHeader.size() + nElements * sizeof(std::complex<double>);
	mm::MemoryMappedFile<mm::CacheHint::SequentialScan, mm::MapMode::ReadAndWrite> mmf("arr1.npy", fileSizeBytes);
	ASSERT_TRUE(mmf.IsValid());
	npypp::Save(mmf, data, shape);

	// rewind
	mmf.Rewind();
	ASSERT_TRUE(mmf.IsValid());

	const auto loadedData = npypp::Load<std::complex<double>>(mmf);
	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData[i]);
}

TEST_F(MmapNpyTests, SaveAndReadFromMappedFileNoCopy)
{
	npypp::Save("arr1.npy", data, shape, "w");

	mm::MemoryMappedFile<mm::CacheHint::SequentialScan> mmf("arr1.npy");
	const auto loadedData = npypp::Load<std::complex<double>>(mmf);

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData[i]);
}

TEST_F(MmapNpyTests, SaveAndReadFullFromMappedFileNoCopy)
{
	npypp::Save("arr1.npy", data, shape, "w");

	auto loadedData = npypp::LoadFull<std::complex<double>>("arr1.npy", true);
	ASSERT_EQ(loadedData.shape.size(), 3);
	ASSERT_EQ(loadedData.shape[0], Nz);
	ASSERT_EQ(loadedData.shape[1], Ny);
	ASSERT_EQ(loadedData.shape[2], Nx);
	ASSERT_EQ(loadedData.data.size(), TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData.data[i]);
}

TEST_F(MmapNpyTests, Append)
{
	npypp::Save("arr1.npy", data, shape, "w");
	npypp::Save("arr1.npy", data, shape, "a");

	auto loadedData = npypp::Load<std::complex<double>>("arr1.npy", true);

	ASSERT_EQ(loadedData.size(), 2 * TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
	{
		ASSERT_TRUE(data[i] == loadedData[i]);
		ASSERT_TRUE(data[i] == loadedData[i + TotalSize]);
	}
}

TEST_F(MmapNpyTests, AppendAndReadFromMappedFile)
{
	npypp::Save("arr1.npy", data, shape, "w");
	npypp::Save("arr1.npy", data, shape, "a");

	mm::MemoryMappedFile<mm::CacheHint::SequentialScan> mmf("arr1.npy");
	const auto loadedData = npypp::Load<std::complex<double>>(mmf);

	for (size_t i = 0; i < TotalSize; i++)
	{
		ASSERT_TRUE(data[i] == loadedData[i]);
		ASSERT_TRUE(data[i] == loadedData[i + TotalSize]);
	}
}

TEST_F(MmapNpyTests, AppendFull)
{
	npypp::Save("arr1.npy", data, shape, "w");
	npypp::Save("arr1.npy", data, shape, "a");

	auto loadedData = npypp::LoadFull<std::complex<double>>("arr1.npy", true);
	ASSERT_EQ(loadedData.shape.size(), 3);
	ASSERT_EQ(loadedData.shape[0], 2 * Nz);
	ASSERT_EQ(loadedData.shape[1], Ny);
	ASSERT_EQ(loadedData.shape[2], Nx);
	ASSERT_EQ(loadedData.data.size(), 2 * TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
	{
		ASSERT_TRUE(data[i] == loadedData.data[i]);
		ASSERT_TRUE(data[i] == loadedData.data[i + TotalSize]);
	}
}

TEST_F(MmapNpyTests, FreshWriteOnlyCreatesAndTruncatesNewFile)
{
	// Ensure WriteOnly works on a brand-new file that did not exist beforehand.
	std::error_code ec;
	std::filesystem::remove("fresh_mmap_write.npy", ec);

	const auto nElements = std::accumulate(shape.begin(), shape.end(), size_t { 1 }, std::multiplies<>());
	const auto npyHeader = npypp::detail::GetNpyHeader<std::complex<double>>(shape);
	const size_t fileSizeBytes = npyHeader.size() + nElements * sizeof(std::complex<double>);
	{
		mm::MemoryMappedFile<mm::CacheHint::SequentialScan, mm::MapMode::WriteOnly> mmf("fresh_mmap_write.npy", fileSizeBytes);
		ASSERT_TRUE(mmf.IsValid());
		npypp::Save(mmf, data, shape);
	}

	const auto loadedData = npypp::Load<std::complex<double>>("fresh_mmap_write.npy");
	ASSERT_EQ(loadedData.size(), TotalSize);
	for (size_t i = 0; i < TotalSize; ++i)
		ASSERT_TRUE(data[i] == loadedData[i]);
}

TEST_F(MmapNpyTests, ZeroCopyLoadSpanAndAlignment)
{
	npypp::Save("aligned_span.npy", data, shape, "w");

	mm::MemoryMappedFile<mm::CacheHint::SequentialScan> mmf("aligned_span.npy");
	ASSERT_TRUE(mmf.IsValid());

	std::vector<size_t> outShape;
	const auto span = npypp::LoadSpan<std::complex<double>>(mmf, &outShape);
	ASSERT_EQ(outShape, shape);
	ASSERT_EQ(span.size(), TotalSize);
	for (size_t i = 0; i < TotalSize; ++i)
		ASSERT_TRUE(span[i] == data[i]);
}

TEST_F(MmapNpyTests, BigEndianLoadViaMmap)
{
	// Write a valid NPY v1.0 header with '>u2' and big-endian payload [0, 1, 2, 3].
	std::string props = "{'descr': '>u2', 'fortran_order': False, 'shape': (2, 2), }";
	props = npypp::detail::PadNpyHeader(std::move(props), 10);
	std::string fileBytes = npypp::detail::GetMagic(1);
	npypp::detail::appendBytes<uint16_t>(fileBytes, static_cast<uint16_t>(props.size()));
	fileBytes += props;
	const std::array<uint8_t, 8> payload { 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03 };
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	fileBytes.append(reinterpret_cast<const char*>(payload.data()), payload.size());

	FILE* fp = OpenTestFile("big_endian_mmap.npy", "wb");
	ASSERT_NE(fp, nullptr);
	ASSERT_EQ(std::fwrite(fileBytes.data(), 1, fileBytes.size(), fp), fileBytes.size());
	std::fclose(fp);

	const auto loaded = npypp::LoadFull<uint16_t>("big_endian_mmap.npy", true);
	ASSERT_EQ(loaded.shape, (std::vector<size_t> { 2, 2 }));
	ASSERT_EQ(loaded.data, (std::vector<uint16_t> { 0, 1, 2, 3 }));
}

TEST_F(MmapNpyTests, BigEndianAllScalarWidthsViaMmapAndFile)
{
	// 1) >i4 (4-byte int32_t)
	{
		std::string props = npypp::detail::PadNpyHeader("{'descr': '>i4', 'fortran_order': False, 'shape': (2, ), }", 10);
		std::string fileBytes = npypp::detail::GetMagic(1);
		npypp::detail::appendBytes<uint16_t>(fileBytes, static_cast<uint16_t>(props.size()));
		fileBytes += props;
		const std::array<uint8_t, 8> payload { 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x01, 0x02 };
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		fileBytes.append(reinterpret_cast<const char*>(payload.data()), payload.size());

		FILE* fp = OpenTestFile("be_types.npy", "wb");
		ASSERT_NE(fp, nullptr);
		ASSERT_EQ(std::fwrite(fileBytes.data(), 1, fileBytes.size(), fp), fileBytes.size());
		std::fclose(fp);

		const auto viaMmap = npypp::LoadFull<int32_t>("be_types.npy", true);
		ASSERT_EQ(viaMmap.data, (std::vector<int32_t> { 7, 258 }));
		const auto viaFile = npypp::LoadFull<int32_t>("be_types.npy", false);
		ASSERT_EQ(viaFile.data, (std::vector<int32_t> { 7, 258 }));
	}

	// 2) >f8 (8-byte double) and >c16 (complex<double>)
	{
		const double orig = 3.25;
		std::vector<uint8_t> raw(sizeof(double));
		std::memcpy(raw.data(), &orig, sizeof(double));
		if constexpr (std::endian::native == std::endian::little)
			ReverseBytes(raw);

		std::string props = npypp::detail::PadNpyHeader("{'descr': '>f8', 'fortran_order': False, 'shape': (1, ), }", 10);
		std::string fileBytes = npypp::detail::GetMagic(1);
		npypp::detail::appendBytes<uint16_t>(fileBytes, static_cast<uint16_t>(props.size()));
		fileBytes += props;
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		fileBytes.append(reinterpret_cast<const char*>(raw.data()), raw.size());

		FILE* fp = OpenTestFile("be_types.npy", "wb");
		ASSERT_NE(fp, nullptr);
		ASSERT_EQ(std::fwrite(fileBytes.data(), 1, fileBytes.size(), fp), fileBytes.size());
		std::fclose(fp);

		const auto loaded = npypp::LoadFull<double>("be_types.npy", true);
		ASSERT_DOUBLE_EQ(loaded.data[0], 3.25);
	}

	// 3) >c16 (complex<double>)
	{
		const std::complex<double> orig(1.5, -2.5);
		const double parts[2] { orig.real(), orig.imag() };
		std::vector<uint8_t> raw(sizeof(parts));
		std::memcpy(raw.data(), parts, sizeof(parts));
		if constexpr (std::endian::native == std::endian::little)
		{
			ReverseBytes(std::span<uint8_t>(raw).first(8));
			ReverseBytes(std::span<uint8_t>(raw).subspan(8));
		}

		std::string props = npypp::detail::PadNpyHeader("{'descr': '>c16', 'fortran_order': False, 'shape': (1, ), }", 10);
		std::string fileBytes = npypp::detail::GetMagic(1);
		npypp::detail::appendBytes<uint16_t>(fileBytes, static_cast<uint16_t>(props.size()));
		fileBytes += props;
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		fileBytes.append(reinterpret_cast<const char*>(raw.data()), raw.size());

		FILE* fp = OpenTestFile("be_types.npy", "wb");
		ASSERT_NE(fp, nullptr);
		ASSERT_EQ(std::fwrite(fileBytes.data(), 1, fileBytes.size(), fp), fileBytes.size());
		std::fclose(fp);

		const auto loaded = npypp::LoadFull<std::complex<double>>("be_types.npy", false);
		ASSERT_EQ(loaded.data[0], orig);
	}

	// 4) Non-1/2/4/8 byte width (>f16 / long double on x86_64 = 16 bytes)
	{
		const long double orig = 7.5L;
		std::vector<uint8_t> raw(sizeof(long double));
		std::memcpy(raw.data(), &orig, sizeof(long double));
		if constexpr (std::endian::native == std::endian::little)
			ReverseBytes(raw);

		std::string descr = "{'descr': '>";
		descr += "f";
		descr += std::to_string(sizeof(long double));
		descr += "', 'fortran_order': False, 'shape': (1, ), }";
		std::string props = npypp::detail::PadNpyHeader(std::move(descr), 10);
		std::string fileBytes = npypp::detail::GetMagic(1);
		npypp::detail::appendBytes<uint16_t>(fileBytes, static_cast<uint16_t>(props.size()));
		fileBytes += props;
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		fileBytes.append(reinterpret_cast<const char*>(raw.data()), raw.size());

		FILE* fp = OpenTestFile("be_types.npy", "wb");
		ASSERT_NE(fp, nullptr);
		ASSERT_EQ(std::fwrite(fileBytes.data(), 1, fileBytes.size(), fp), fileBytes.size());
		std::fclose(fp);

		const auto loaded = npypp::LoadFull<long double>("be_types.npy", true);
		ASSERT_EQ(loaded.data[0], orig);
	}
}

TEST_F(MmapNpyTests, MemoryMappedFileEdgeCasesAndHints)
{
	// Missing ReadOnly file
	{
		mm::MemoryMappedFile<mm::CacheHint::SequentialScan, mm::MapMode::ReadOnly> missing("does_not_exist_mmap.npy");
		ASSERT_FALSE(missing.IsValid());
		ASSERT_EQ(missing.RemainingBytes(), 0u);
		ASSERT_FALSE(missing.ReMap(0, 4));
	}

	// Empty 0-byte regular file fails ReMap during Open and cleans up
	{
		FILE* fp = OpenTestFile("empty_file.npy", "wb");
		ASSERT_NE(fp, nullptr);
		std::fclose(fp);
		mm::MemoryMappedFile<mm::CacheHint::SequentialScan, mm::MapMode::ReadOnly> emptyMmf("empty_file.npy");
		ASSERT_FALSE(emptyMmf.IsValid());
	}

	// WriteOnly with /dev/null and initialMappedBytes > 0 fails ftruncate64
	{
		mm::MemoryMappedFile<mm::CacheHint::Normal, mm::MapMode::WriteOnly> devNull("/dev/null", 16);
		ASSERT_FALSE(devNull.IsValid());
	}

	// Invalid enum MapMode
	{
		mm::MemoryMappedFile<mm::CacheHint::SequentialScan, static_cast<mm::MapMode>(99)> badMode("small_rw.npy");
		ASSERT_FALSE(badMode.IsValid());
	}

	// ReMap, Open when already valid, ReadFrom/CopyTo 0 bytes, CacheHints
	{
		mm::MemoryMappedFile<mm::CacheHint::Normal, mm::MapMode::ReadAndWrite> rw("small_rw.npy", 16);
		ASSERT_TRUE(rw.IsValid());
		ASSERT_FALSE(rw.Open());
		rw.ReadFrom(nullptr, 0);
		rw.CopyTo<uint8_t>(nullptr, 0);

		const std::array<uint8_t, 8> bytes { 1, 2, 3, 4, 5, 6, 7, 8 };
		rw.ReadFrom(bytes.data(), bytes.size());

		// ReMap closes old mapping and clamps mappedBytes to fileSize
		ASSERT_TRUE(rw.ReMap(0, 4));
		ASSERT_EQ(rw.MappedSize(), 4u);
		ASSERT_TRUE(rw.ReMap(0, 1000000));
		ASSERT_EQ(rw.MappedSize(), 16u);
		// offset >= fileSize
		ASSERT_FALSE(rw.ReMap(1000000, 4));
		// unaligned page offset causes mmap failure
		ASSERT_FALSE(rw.ReMap(1, 4));
	}

	// CacheHint::RandomAccess and default hint fallback
	{
		mm::MemoryMappedFile<mm::CacheHint::RandomAccess, mm::MapMode::ReadOnly> ra("small_rw.npy");
		ASSERT_TRUE(ra.IsValid());
		ASSERT_EQ(ra[0], 1);

		mm::MemoryMappedFile<static_cast<mm::CacheHint>(99), mm::MapMode::ReadOnly> defHint("small_rw.npy");
		ASSERT_TRUE(defHint.IsValid());
		ASSERT_EQ(defHint[1], 2);
	}

	// Load pointer overload (zero-copy)
	{
		const std::vector<double> vals { 10.0, 20.0, 30.0 };
		npypp::Save("aligned_span.npy", vals, std::vector<size_t> { 3 }, "w");
		mm::MemoryMappedFile<mm::CacheHint::SequentialScan> mmf("aligned_span.npy");
		const double* ptr = nullptr;
		npypp::Load(ptr, mmf);
		ASSERT_NE(ptr, nullptr);
		ASSERT_DOUBLE_EQ(ptr[0], 10.0);
		ASSERT_DOUBLE_EQ(ptr[2], 30.0);
	}
}

TEST_F(MmapNpyTests, V2HeaderRoundTripViaFileAndMmap)
{
	// > 22,000 dims of size 1 creates a header > 65535 bytes, automatically switching to NPY v2.0.
	const std::vector<size_t> deepShape(22000, 1);
	const std::vector<int32_t> vals { 42 };
	npypp::Save("v2_mmap.npy", vals, deepShape, "w");

	const auto loadedFile = npypp::LoadFull<int32_t>("v2_mmap.npy", false);
	ASSERT_EQ(loadedFile.shape.size(), 22000u);
	ASSERT_EQ(loadedFile.data, vals);

	const auto loadedMmap = npypp::LoadFull<int32_t>("v2_mmap.npy", true);
	ASSERT_EQ(loadedMmap.shape.size(), 22000u);
	ASSERT_EQ(loadedMmap.data, vals);
}
