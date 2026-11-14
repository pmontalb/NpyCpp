#include "pch.h"
#include <Npy++.h>

#include <complex>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <zlib.h>

constexpr size_t Nx { 128 };
constexpr size_t Ny { 64 };
constexpr size_t Nz { 32 };
constexpr size_t TotalSize { Nx * Ny * Nz };
const std::vector<size_t> shape { Nz, Ny, Nx };

class NpzTests: public ::testing::Test
{
public:
	NpzTests() : data(Nx * Ny * Nz) {}

	void SetUp() override
	{
		for (size_t i = 0; i < Nx * Ny * Nz; i++)
			data[i] = std::complex<double>(rand(), rand());
	}

	void TearDown() override
	{
		std::error_code ec;
		std::filesystem::remove("arr1.npz", ec);
		std::filesystem::remove("out.npz", ec);
		std::filesystem::remove("deflated.npz", ec);
		std::filesystem::remove("be_deflated.npz", ec);
		std::filesystem::remove("garbage.npz", ec);
	}

protected:
	std::vector<std::complex<double>> data;
};

TEST_F(NpzTests, ReadAndSave)
{
	npypp::SaveCompressed("arr1.npz", data, shape, "w");

	auto loadedData = npypp::LoadCompressed<std::complex<double>>("arr1.npz", "arr1");

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData[i]);
}

TEST_F(NpzTests, ReadAndSaveFull)
{
	npypp::SaveCompressed("arr1.npz", data, shape, "w");

	auto loadedData = npypp::LoadCompressedFull<std::complex<double>>("arr1.npz", "arr1");
	ASSERT_EQ(loadedData.shape.size(), 3);
	ASSERT_EQ(loadedData.shape[0], Nz);
	ASSERT_EQ(loadedData.shape[1], Ny);
	ASSERT_EQ(loadedData.shape[2], Nx);
	ASSERT_EQ(loadedData.data.size(), TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData.data[i]);
}

TEST_F(NpzTests, Append)
{
	npypp::SaveCompressed("out.npz", "arr1", data, shape, "w");
	npypp::SaveCompressed("out.npz", "arr2", data, shape, "a");

	auto dataDictionary = npypp::LoadCompressed<std::complex<double>>("out.npz");

	const auto& loadedData1 = dataDictionary["arr1"];
	const auto& loadedData2 = dataDictionary["arr2"];

	ASSERT_EQ(loadedData1.size(), TotalSize);
	ASSERT_EQ(loadedData2.size(), TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
	{
		ASSERT_TRUE(data[i] == loadedData1[i]);
		ASSERT_TRUE(data[i] == loadedData2[i]);
	}
}

TEST_F(NpzTests, AppendFull)
{
	npypp::SaveCompressed("out.npz", "arr1", data, shape, "w");
	npypp::SaveCompressed("out.npz", "arr2", data, shape, "a");

	auto dataDictionary = npypp::LoadCompressedFull<std::complex<double>>("out.npz");

	const auto& loadedData1 = dataDictionary["arr1"];
	const auto& loadedData2 = dataDictionary["arr2"];

	ASSERT_EQ(loadedData1.shape.size(), 3);
	ASSERT_EQ(loadedData1.shape[0], Nz);
	ASSERT_EQ(loadedData1.shape[1], Ny);
	ASSERT_EQ(loadedData1.shape[2], Nx);
	ASSERT_EQ(loadedData1.data.size(), TotalSize);

	ASSERT_EQ(loadedData2.shape.size(), 3);
	ASSERT_EQ(loadedData2.shape[0], Nz);
	ASSERT_EQ(loadedData2.shape[1], Ny);
	ASSERT_EQ(loadedData2.shape[2], Nx);
	ASSERT_EQ(loadedData2.data.size(), TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
	{
		ASSERT_TRUE(data[i] == loadedData1.data[i]);
		ASSERT_TRUE(data[i] == loadedData2.data[i]);
	}
}

TEST_F(NpzTests, ReadBigEndian)
{
	auto arrayDictionary = npypp::LoadCompressedFull<uint16_t>("0123.npz");
	ASSERT_EQ(arrayDictionary.size(), 1);
	auto iter = arrayDictionary.begin();
	ASSERT_EQ(iter->first, "x");
	auto array = iter->second;
	auto arrayShape = array.shape;
	for (size_t i = 0; i < arrayShape.size(); i++)
		ASSERT_EQ(arrayShape[i], 2);
	auto d = array.data;
	for (size_t i = 0; i < d.size(); i++)
		ASSERT_EQ(d[i], i);
}

TEST_F(NpzTests, SingleVariableLookupSkipsOtherEntries)
{
	npypp::SaveCompressed("out.npz", "first", data, shape, "w");
	const std::vector<std::complex<double>> second { { 1.5, -2.5 }, { 3.5, 4.5 } };
	npypp::SaveCompressed("out.npz", "second", second, std::vector<size_t> { 2 }, "a");

	const auto loadedSecond = npypp::LoadCompressedFull<std::complex<double>>("out.npz", "second");
	ASSERT_EQ(loadedSecond.shape, (std::vector<size_t> { 2 }));
	ASSERT_EQ(loadedSecond.data, second);

	const auto missing = npypp::LoadCompressedFull<std::complex<double>>("out.npz", "nonexistent");
	ASSERT_TRUE(missing.data.empty());
}

TEST_F(NpzTests, ReadDeflateCompressedNpz)
{
	// Build a real deflate-compressed (method=8) .npz entry using zlib.
	const std::vector<double> vec { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
	const std::vector<size_t> vecShape { 2, 3 };
	const std::string npyHeader = npypp::detail::GetNpyHeader<double>(vecShape);

	std::string rawNpy = npyHeader;
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	rawNpy.append(reinterpret_cast<const char*>(vec.data()), vec.size() * sizeof(double));

	z_stream zs {};
	ASSERT_EQ(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY), Z_OK);
	std::vector<unsigned char> compressed(rawNpy.size() + 64);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	zs.next_in = reinterpret_cast<Bytef*>(rawNpy.data());
	zs.avail_in = static_cast<uInt>(rawNpy.size());
	zs.next_out = compressed.data();
	zs.avail_out = static_cast<uInt>(compressed.size());
	ASSERT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
	const auto compSize = static_cast<uint32_t>(zs.total_out);
	deflateEnd(&zs);
	compressed.resize(compSize);

	const uint32_t crc = npypp::detail::GetCrcNpyFile(npyHeader, vec);
	const std::string entryName = "arr.npy";

	std::string localHeader = "PK";
	npypp::detail::appendBytes<uint16_t>(localHeader, 0x0403);
	npypp::detail::appendBytes<uint16_t>(localHeader, 20);
	npypp::detail::appendBytes<uint16_t>(localHeader, 0);
	npypp::detail::appendBytes<uint16_t>(localHeader, 8);	// deflate
	npypp::detail::appendBytes<uint16_t>(localHeader, 0);
	npypp::detail::appendBytes<uint16_t>(localHeader, 0);
	npypp::detail::appendBytes<uint32_t>(localHeader, crc);
	npypp::detail::appendBytes<uint32_t>(localHeader, compSize);
	npypp::detail::appendBytes<uint32_t>(localHeader, static_cast<uint32_t>(rawNpy.size()));
	npypp::detail::appendBytes<uint16_t>(localHeader, static_cast<uint16_t>(entryName.size()));
	npypp::detail::appendBytes<uint16_t>(localHeader, 0);
	localHeader += entryName;

	const std::string globalHeader = npypp::detail::MakeGlobalHeaderEntry(localHeader, 0, entryName);
	const std::string footer = npypp::detail::GetNpzFooter(globalHeader, 0, static_cast<uint32_t>(localHeader.size()), compSize, 1);

	FILE* fp = std::fopen("deflated.npz", "wb");
	ASSERT_NE(fp, nullptr);
	std::fwrite(localHeader.data(), 1, localHeader.size(), fp);
	std::fwrite(compressed.data(), 1, compressed.size(), fp);
	std::fwrite(globalHeader.data(), 1, globalHeader.size(), fp);
	std::fwrite(footer.data(), 1, footer.size(), fp);
	std::fclose(fp);

	const auto loaded = npypp::LoadCompressedFull<double>("deflated.npz", "arr");
	ASSERT_EQ(loaded.shape, vecShape);
	ASSERT_EQ(loaded.data, vec);
}

TEST_F(NpzTests, ReadBigEndianDeflateCompressedNpzAndEdgeCases)
{
	// Deflate-compressed big-endian >f8 entry exercises SwapEndianness inside LoadCompressedFull.
	const std::vector<double> vec { 1.5, -2.5 };
	const std::string paddedProps = npypp::detail::PadNpyHeader("{'descr': '>f8', 'fortran_order': False, 'shape': (2, ), }", 10);
	std::string npyHeader = npypp::detail::GetMagic(1);
	npypp::detail::appendBytes<uint16_t>(npyHeader, static_cast<uint16_t>(paddedProps.size()));
	npyHeader += paddedProps;
	std::string rawNpy = npyHeader;
	std::vector<uint8_t> rawData(vec.size() * sizeof(double));
	std::memcpy(rawData.data(), vec.data(), rawData.size());
	if constexpr (std::endian::native == std::endian::little)
	{
		for (size_t i = 0; i < vec.size(); ++i)
			std::reverse(rawData.data() + i * sizeof(double), rawData.data() + (i + 1) * sizeof(double));
	}
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	rawNpy.append(reinterpret_cast<const char*>(rawData.data()), rawData.size());

	z_stream zs {};
	ASSERT_EQ(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY), Z_OK);
	std::vector<unsigned char> compressed(rawNpy.size() + 64);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	zs.next_in = reinterpret_cast<Bytef*>(rawNpy.data());
	zs.avail_in = static_cast<uInt>(rawNpy.size());
	zs.next_out = compressed.data();
	zs.avail_out = static_cast<uInt>(compressed.size());
	ASSERT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
	const auto compSize = static_cast<uint32_t>(zs.total_out);
	deflateEnd(&zs);
	compressed.resize(compSize);

	const std::string entryName = "be.npy";
	std::string localHeader = "PK";
	npypp::detail::appendBytes<uint16_t>(localHeader, 0x0403);
	npypp::detail::appendBytes<uint16_t>(localHeader, 20);
	npypp::detail::appendBytes<uint16_t>(localHeader, 0);
	npypp::detail::appendBytes<uint16_t>(localHeader, 8);
	npypp::detail::appendBytes<uint16_t>(localHeader, 0);
	npypp::detail::appendBytes<uint16_t>(localHeader, 0);
	npypp::detail::appendBytes<uint32_t>(localHeader, 0);
	npypp::detail::appendBytes<uint32_t>(localHeader, compSize);
	npypp::detail::appendBytes<uint32_t>(localHeader, static_cast<uint32_t>(rawNpy.size()));
	npypp::detail::appendBytes<uint16_t>(localHeader, static_cast<uint16_t>(entryName.size()));
	npypp::detail::appendBytes<uint16_t>(localHeader, 0);
	localHeader += entryName;

	const std::string globalHeader = npypp::detail::MakeGlobalHeaderEntry(localHeader, 0, entryName);
	const std::string footer = npypp::detail::GetNpzFooter(globalHeader, 0, static_cast<uint32_t>(localHeader.size()), compSize, 1);

	FILE* fp = std::fopen("be_deflated.npz", "wb");
	ASSERT_NE(fp, nullptr);
	std::fwrite(localHeader.data(), 1, localHeader.size(), fp);
	std::fwrite(compressed.data(), 1, compressed.size(), fp);
	std::fwrite(globalHeader.data(), 1, globalHeader.size(), fp);
	std::fwrite(footer.data(), 1, footer.size(), fp);
	std::fclose(fp);

	const auto loaded = npypp::LoadCompressedFull<double>("be_deflated.npz", "be");
	ASSERT_EQ(loaded.data, vec);

	// Valid .npz with a trailing ZIP archive comment exercises backwards EOCD scan in ParseNpzFooter
	std::string commentedFooter = footer;
	const std::string zipComment = "hello";
	commentedFooter[20] = static_cast<char>(zipComment.size());
	commentedFooter[21] = 0;
	commentedFooter += zipComment;
	FILE* cfp = std::fopen("commented.npz", "wb");
	ASSERT_NE(cfp, nullptr);
	std::fwrite(localHeader.data(), 1, localHeader.size(), cfp);
	std::fwrite(compressed.data(), 1, compressed.size(), cfp);
	std::fwrite(globalHeader.data(), 1, globalHeader.size(), cfp);
	std::fwrite(commentedFooter.data(), 1, commentedFooter.size(), cfp);
	std::fclose(cfp);
	const auto commentedLoaded = npypp::LoadCompressedFull<double>("commented.npz", "be");
	ASSERT_EQ(commentedLoaded.data, vec);

	// Invalid EOCD (22 bytes of zeros) exercises ParseNpzFooter failure path
	FILE* gfp = std::fopen("garbage.npz", "wb");
	ASSERT_NE(gfp, nullptr);
	const std::array<uint8_t, 22> zeros {};
	std::fwrite(zeros.data(), 1, zeros.size(), gfp);
	std::fclose(gfp);
	NPY_EXPECT_DEATH(npypp::LoadCompressedFull<int>("garbage.npz"), "EOCD signature not found");

	NPY_EXPECT_DEATH(npypp::SaveCompressed("/tmp/", std::vector<int> { 1 }, std::vector<size_t> { 1 }), "Failed to open .npz file for writing");
}
