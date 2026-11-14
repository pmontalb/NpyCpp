#include "pch.h"

#include "IgnoreWarning.h"

#ifdef __clang__
	#define __IGNORE_CNPY_WARNINGS                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             \
		__IGNORE_WARNING__("-Wold-style-cast")                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 \
		__IGNORE_WARNING__("-Wzero-as-null-pointer-constant")                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  \
		__IGNORE_WARNING__("-Wsign-conversion")                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                \
		__IGNORE_WARNING__("-Wcast-qual")                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      \
		__IGNORE_WARNING__("-Wshorten-64-to-32")
#elif defined(_MSC_VER)
	#define __IGNORE_CNPY_WARNINGS                                                                                                                                                                     \
		__IGNORE_WARNING__(4267)                                                                                                                                                                       \
		__IGNORE_WARNING__(4310)                                                                                                                                                                       \
		__IGNORE_WARNING__(4365)
#else
	#define __IGNORE_CNPY_WARNINGS
#endif

__START_IGNORING_WARNINGS__
__IGNORE_CNPY_WARNINGS
#include "cnpy/cnpy.h"
__STOP_IGNORING_WARNINGS__

#include <complex>
#include <cstdlib>
#include <map>

constexpr size_t Nx { 128 };
constexpr size_t Ny { 64 };
constexpr size_t Nz { 32 };
const std::vector<size_t> shape { Nz, Ny, Nx };

class cnpyTests: public ::testing::Test
{
public:
	cnpyTests() : data(Nx * Ny * Nz) {}

	void SetUp() override
	{
		for (size_t i = 0; i < Nx * Ny * Nz; i++)
			data[i] = std::complex<double>(rand(), rand());
	}

protected:
	std::vector<std::complex<double>> data;
};

__START_IGNORING_WARNINGS__
__IGNORE_CNPY_WARNINGS
TEST_F(cnpyTests, ReadAndSave)
{
	// save it to file
	cnpy::npy_save("arr1.npy", &data[0], shape, "w");

	// load it into a new array
	cnpy::NpyArray arr = cnpy::npy_load("arr1.npy");
	std::complex<double>* loaded_data = arr.data<std::complex<double>>();

	// make sure the loaded data matches the saved data
	ASSERT_TRUE(arr.word_size == sizeof(std::complex<double>));
	ASSERT_TRUE(arr.shape.size() == 3 && arr.shape[0] == Nz && arr.shape[1] == Ny && arr.shape[2] == Nx);

	for (size_t i = 0; i < Nx * Ny * Nz; i++)
		ASSERT_TRUE(data[i] == loaded_data[i]);
}

TEST_F(cnpyTests, Append)
{
	// save it to file
	cnpy::npy_save("arr1.npy", &data[0], shape, "w");

	// append the same data to file
	// npy array on file now has shape (Nz+Nz,Ny,Nx)
	cnpy::npy_save("arr1.npy", &data[0], { Nz, Ny, Nx }, "a");

	// now write to an npz file
	// non-array variables are treated as 1D arrays with 1 element
	double myVar1 = 1.2;
	char myVar2 = 'a';
	cnpy::npz_save("out.npz", "myVar1", &myVar1, { 1 }, "w");			 //"w" overwrites any existing file
	cnpy::npz_save("out.npz", "myVar2", &myVar2, { 1 }, "a");			 //"a" appends to the file we created above
	cnpy::npz_save("out.npz", "arr1", &data[0], { Nz, Ny, Nx }, "a");	 //"a" appends to the file we created above

	cnpy::NpyArray arr2 = cnpy::npz_load("out.npz", "arr1");

	// load the entire npz file
	cnpy::npz_t my_npz = cnpy::npz_load("out.npz");

	// check that the loaded myVar1 matches myVar1
	cnpy::NpyArray arr_mv1 = my_npz["myVar1"];
	auto* mv1 = arr_mv1.data<double>();
	ASSERT_TRUE(arr_mv1.shape.size() == 1 && arr_mv1.shape[0] == 1);
	ASSERT_DOUBLE_EQ(mv1[0], myVar1);
}
__STOP_IGNORING_WARNINGS__
