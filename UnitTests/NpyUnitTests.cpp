#include "pch.h"

#include "IgnoreWarning.h"

#ifdef __clang__
	#if __clang_major__ >= 23
		#define __IGNORE_CLANG23_CNPY_WARNINGS                                                                                                                                                             \
			__IGNORE_WARNING__("-Wlifetime-safety-invalidation")                                                                                                                                           \
			__IGNORE_WARNING__("-Wlifetime-safety-intra-tu-suggestions")
	#else
		#define __IGNORE_CLANG23_CNPY_WARNINGS
	#endif
	#define __IGNORE_CNPY_WARNINGS                                                                                                                                                                     \
		__IGNORE_WARNING__("-Wold-style-cast")                                                                                                                                                         \
		__IGNORE_WARNING__("-Wzero-as-null-pointer-constant")                                                                                                                                          \
		__IGNORE_WARNING__("-Wsign-conversion")                                                                                                                                                        \
		__IGNORE_WARNING__("-Wcast-qual")                                                                                                                                                              \
		__IGNORE_WARNING__("-Wshorten-64-to-32")                                                                                                                                                       \
		__IGNORE_WARNING__("-Wimplicit-int-conversion")                                                                                                                                                \
		__IGNORE_WARNING__("-Wimplicit-float-conversion")                                                                                                                                              \
		__IGNORE_WARNING__("-Wcast-align")                                                                                                                                                             \
		__IGNORE_CLANG23_CNPY_WARNINGS
#elif defined(_MSC_VER)
	#define __IGNORE_CNPY_WARNINGS                                                                                                                                                                     \
		__IGNORE_WARNING__(4267)                                                                                                                                                                       \
		__IGNORE_WARNING__(4310)                                                                                                                                                                       \
		__IGNORE_WARNING__(4365)
#else
	#define __IGNORE_CNPY_WARNINGS                                                                                                                                                                     \
		__IGNORE_WARNING__("-Wold-style-cast")                                                                                                                                                         \
		__IGNORE_WARNING__("-Wzero-as-null-pointer-constant")                                                                                                                                          \
		__IGNORE_WARNING__("-Wsign-conversion")                                                                                                                                                        \
		__IGNORE_WARNING__("-Wconversion")                                                                                                                                                             \
		__IGNORE_WARNING__("-Warith-conversion")                                                                                                                                                       \
		__IGNORE_WARNING__("-Wcast-align")                                                                                                                                                             \
		__IGNORE_WARNING__("-Wcast-qual")                                                                                                                                                              \
		__IGNORE_WARNING__("-Weffc++")
#endif

__START_IGNORING_WARNINGS__
__IGNORE_CNPY_WARNINGS
#include "cnpy/cnpy.h"
__STOP_IGNORING_WARNINGS__

#include <Npy++.h>

#include <complex>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <stdexcept>

constexpr size_t Nx { 128 };
constexpr size_t Ny { 64 };
constexpr size_t Nz { 32 };
constexpr size_t TotalSize { Nx * Ny * Nz };
const std::vector<size_t> shape { Nz, Ny, Nx };

class NpyTests: public ::testing::Test
{
public:
	NpyTests() : data(Nx * Ny * Nz) {}

	void SetUp() override
	{
		for (size_t i = 0; i < Nx * Ny * Nz; i++)
			data[i] = std::complex<double>(rand(), rand());
	}

	void TearDown() override
	{
		std::error_code ec;
		std::filesystem::remove("v.npy", ec);
		std::filesystem::remove("v.cnpy", ec);
		std::filesystem::remove("arr1.npy", ec);
		std::filesystem::remove("scalar.npy", ec);
		std::filesystem::remove("long_header.npy", ec);
		std::filesystem::remove("append_grow.npy", ec);
		std::filesystem::remove("enum_mode.npy", ec);
		std::filesystem::remove("eq_endian.npy", ec);
	}

protected:
	std::vector<std::complex<double>> data;
};

__START_IGNORING_WARNINGS__
__IGNORE_CNPY_WARNINGS
TEST_F(NpyTests, OriginalLibraryConsistency)
{
	npypp::Save("v.npy", data, shape, "w");
	cnpy::npy_save("v.cnpy", data.data(), shape, "w");

	auto loadedData = npypp::Load<std::complex<double>>("v.npy");
	auto loadedData2 = npypp::Load<std::complex<double>>("v.cnpy");

	for (size_t i = 0; i < TotalSize; i++)
	{
		ASSERT_TRUE(data[i] == loadedData[i]);
		ASSERT_TRUE(data[i] == loadedData2[i]);
	}
}
__STOP_IGNORING_WARNINGS__

TEST_F(NpyTests, ReadAndSave)
{
	npypp::Save("arr1.npy", data, shape, "w");

	auto loadedData = npypp::Load<std::complex<double>>("arr1.npy");

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData[i]);
}

TEST_F(NpyTests, ReadAndSaveFull)
{
	npypp::Save("arr1.npy", data, shape, "w");

	auto loadedData = npypp::LoadFull<std::complex<double>>("arr1.npy");
	ASSERT_EQ(loadedData.shape.size(), 3);
	ASSERT_EQ(loadedData.shape[0], Nz);
	ASSERT_EQ(loadedData.shape[1], Ny);
	ASSERT_EQ(loadedData.shape[2], Nx);
	ASSERT_EQ(loadedData.data.size(), TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
		ASSERT_TRUE(data[i] == loadedData.data[i]);
}

TEST_F(NpyTests, Append)
{
	npypp::Save("arr1.npy", data, shape, "w");
	npypp::Save("arr1.npy", data, shape, "a");

	auto loadedData = npypp::Load<std::complex<double>>("arr1.npy");

	ASSERT_EQ(loadedData.size(), 2 * TotalSize);

	for (size_t i = 0; i < TotalSize; i++)
	{
		ASSERT_TRUE(data[i] == loadedData[i]);
		ASSERT_TRUE(data[i] == loadedData[i + TotalSize]);
	}
}

TEST_F(NpyTests, AppendFull)
{
	npypp::Save("arr1.npy", data, shape, "w");
	npypp::Save("arr1.npy", data, shape, "a");

	auto loadedData = npypp::LoadFull<std::complex<double>>("arr1.npy");
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

TEST_F(NpyTests, ScalarZeroDim)
{
	const std::vector<double> scalar { 42.5 };
	const std::vector<size_t> emptyShape {};
	npypp::Save("scalar.npy", scalar, emptyShape, "w");

	const auto loaded = npypp::LoadFull<double>("scalar.npy");
	ASSERT_TRUE(loaded.shape.empty());
	ASSERT_EQ(loaded.data.size(), 1u);
	ASSERT_DOUBLE_EQ(loaded.data[0], 42.5);
}

TEST_F(NpyTests, LongHeaderOver256Bytes)
{
	// 40-dimensional shape produces a header well beyond 256 bytes.
	std::vector<size_t> deepShape(40, 1);
	deepShape.back() = 3;
	const std::vector<int32_t> vals { 7, 11, 13 };
	npypp::Save("long_header.npy", vals, deepShape, "w");

	const auto loaded = npypp::LoadFull<int32_t>("long_header.npy");
	ASSERT_EQ(loaded.shape, deepShape);
	ASSERT_EQ(loaded.data, vals);

	const auto loadedMmap = npypp::LoadFull<int32_t>("long_header.npy", true);
	ASSERT_EQ(loadedMmap.shape, deepShape);
	ASSERT_EQ(loadedMmap.data, vals);
}

TEST_F(NpyTests, AppendAcrossHeaderPaddingGrowth)
{
	// 4 dims of size 1 has unpadded header size 78 -> padded to 80; growing dim[0] from 1 -> 1000 pushes past 80 bytes to 96 bytes.
	const std::vector<size_t> initialShape { 1, 1, 1, 1 };
	const std::vector<size_t> grownShape { 1000, 1, 1, 1 };
	ASSERT_GT(npypp::detail::GetNpyHeader<int32_t>(grownShape).size(), npypp::detail::GetNpyHeader<int32_t>(initialShape).size());

	const std::vector<int32_t> initial { 123 };
	npypp::Save("append_grow.npy", initial, initialShape, "w");

	std::vector<int32_t> batch(999);
	for (size_t i = 0; i < batch.size(); ++i)
		batch[i] = static_cast<int32_t>(i + 200);
	npypp::Save("append_grow.npy", batch, std::vector<size_t> { 999, 1, 1, 1 }, "a");

	const auto loaded = npypp::LoadFull<int32_t>("append_grow.npy");
	ASSERT_EQ(loaded.shape, grownShape);
	ASSERT_EQ(loaded.data.size(), 1000u);
	ASSERT_EQ(loaded.data[0], 123);
	ASSERT_EQ(loaded.data[1], 200);
	ASSERT_EQ(loaded.data[999], 1198);
}

TEST_F(NpyTests, ConvenienceTypesMatrixAndTensor)
{
	const npypp::Matrix<int> mat({ 1, 2, 3, 4, 5, 6 }, 2, 3);
	ASSERT_EQ(mat.shape, (std::vector<size_t> { 2, 3 }));
	ASSERT_EQ(mat.data.size(), 6u);

	const npypp::Tensor<float> ten({ 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F }, 2, 2, 2);
	ASSERT_EQ(ten.shape, (std::vector<size_t> { 2, 2, 2 }));
	ASSERT_EQ(ten.data.size(), 8u);
}

TEST_F(NpyTests, EnumeratorsAndEqualSignEndianness)
{
	ASSERT_STREQ(ToString(FileOpenMode::Read), "r");
	ASSERT_STREQ(ToString(FileOpenMode::Write), "w");
	ASSERT_STREQ(ToString(FileOpenMode::Append), "a");
	ASSERT_STREQ(ToString(static_cast<FileOpenMode>(99)), "?");

	const npypp::Vector<int32_t> vec({ 1, 2 });
	npypp::Save("enum_mode.npy", vec, FileOpenMode::Write);
	npypp::Save("enum_mode.npy", std::vector<int32_t> { 3, 4 }, std::vector<size_t> { 2 }, FileOpenMode::Append);
	const auto loadedEnum = npypp::LoadFull<int32_t>("enum_mode.npy");
	ASSERT_EQ(loadedEnum.data, (std::vector<int32_t> { 1, 2, 3, 4 }));

	// Test '=' native endianness marker in descr
	std::string props = npypp::detail::PadNpyHeader("{'descr': '=i4', 'fortran_order': False, 'shape': (2, ), }", 10);
	std::string fileBytes = npypp::detail::GetMagic(1);
	npypp::detail::appendBytes<uint16_t>(fileBytes, static_cast<uint16_t>(props.size()));
	fileBytes += props;
	const std::array<int32_t, 2> vals { 11, -22 };
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	fileBytes.append(reinterpret_cast<const char*>(vals.data()), sizeof(vals));

	FILE* fp = OpenTestFile("eq_endian.npy", "wb");
	ASSERT_NE(fp, nullptr);
	ASSERT_EQ(std::fwrite(fileBytes.data(), 1, fileBytes.size(), fp), fileBytes.size());
	std::fclose(fp);

	const auto loadedEq = npypp::LoadFull<int32_t>("eq_endian.npy");
	ASSERT_EQ(loadedEq.data, (std::vector<int32_t> { 11, -22 }));
}

TEST_F(NpyTests, MissingFileAborts)
{
	NPY_EXPECT_DEATH(npypp::LoadFull<double>("does_not_exist.npy"), "Failed to open .npy file");
	NPY_EXPECT_DEATH(npypp::LoadFull<double>("does_not_exist.npy", true), "Failed to memory-map .npy file");
}
