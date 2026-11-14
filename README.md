[![CI](https://github.com/pmontalb/NpyCpp/actions/workflows/ci.yml/badge.svg)](https://github.com/pmontalb/NpyCpp/actions/workflows/ci.yml)
[![Coverage](https://github.com/pmontalb/NpyCpp/actions/workflows/coverage.yml/badge.svg)](https://github.com/pmontalb/NpyCpp/actions/workflows/coverage.yml)
[![codecov](https://codecov.io/gh/pmontalb/NpyCpp/branch/master/graph/badge.svg?token=ASHPB5M8UR)](https://codecov.io/gh/pmontalb/NpyCpp)

# Npy++
A modern **C++20** library for reading and writing NumPy `.npy` and `.npz` files, originally forked from [rogersce/cnpy](https://github.com/rogersce/cnpy/) (see original [README](https://github.com/rogersce/cnpy/blob/master/README.md)).

## Implemented Functionalities
- **No RTTI & No Exceptions (`-fno-rtti`, `-fno-exceptions`):** Uses compile-time `npypp::detail::Traits<T>` instead of `std::type_info`, and reports fatal I/O or format errors via `std::cerr`, `assert(false)`, and `std::abort()`.
- **`.npy` File Support (`Save`, `Append`, `Load`, `LoadFull`):**
  - Reads `.npy` format v1.0, v2.0, and v3.0 headers; writes v1.0 (or v2.0 automatically for long headers).
  - Supports `float`, `double`, `long double`, signed/unsigned integers (`char`/`short`/`int`/`long`/`long long`), `bool`, and `std::complex<T>`.
  - Handles `<`, `>`, and `=` endianness automatically (byte-swapping when needed).
  - Supports both mode strings (`"w"`, `"a"`) and typed `FileOpenMode` (`Write`, `Append`, `Read`).
- **Convenience Containers:** `MultiDimensionalArray<T>`, `Vector<T>`, `Matrix<T>`, and `Tensor<T>` (accepted directly by `npypp::Save`).
- **`.npz` Archive Support (`SaveCompressed`, `LoadCompressed`, `LoadCompressedFull`):**
  - Writes and appends named arrays into ZIP archives (uncompressed Zip32 storage).
  - Reads both stored (uncompressed) and `zlib`-deflated `.npz` entries.
  - Supports loading a single named variable (`LoadCompressed<T>(zipFileName, vectorName)` / `LoadCompressedFull<T>(...)`) without decompressing unrelated entries.
- **Memory-Mapped `.npy` I/O (`mm::MemoryMappedFile`):**
  - Supports read-only and read/write memory mapping for `.npy` files (`useMemoryMap = true` or explicit `mm::MemoryMappedFile`).
  - Zero-copy views directly into mapped files via `npypp::LoadSpan<T>(mmf, &shape)` (`std::span<const T>`) and `npypp::Load<T>(ptr, mmf)`.
- **Build & Tests:** CMake 3.28+ preset integration via `cmake/` ([CmakeUtilities](https://github.com/pmontalb/CmakeUtilities)) and GoogleTest unit test suites (`NpyTests`, `cNpyTests`, `NpyMsanSmoke`).

### Known Limitations
- Multi-dimensional arrays must be C-contiguous (`fortran_order: False`).
- When loading an entire `.npz` archive into a map (`LoadCompressed<T>` / `LoadCompressedFull<T>`), all entries share the template element type `T`.
- Memory-mapped I/O is for `.npy` files (not `.npz` archives). `.npz` writing uses stored Zip32 entries (`< 4 GiB`).
- Zero-copy `LoadSpan` / pointer `Load` requires host-native endianness, matching element size, C-order, and natural `alignof(T)` alignment in the mapped buffer.

## Building & Testing
Dependencies: CMake `3.28+`, Ninja, `zlib` (`zlib1g-dev`), and `GoogleTest` (`libgtest-dev`, top-level builds only).

```bash
cmake -S . --preset=gcc-release
cmake --build --preset=gcc-release
ctest --preset=gcc-release --output-on-failure
```

See [cmake/README.md](cmake/README.md) for all available compiler, sanitizer, LTO, PGO, coverage, and CUDA presets.

## Sample Usage

### Basic `.npy` and `.npz` Serialization
```cpp
#include <Npy++.h>

#include <cassert>
#include <vector>

int main()
{
    const std::vector<size_t> shape { 4, 4, 8 };
    const std::vector<double> vec(128, 0.01);

    // Serialize using stdio (FILE*)
    npypp::Save("arr1.npy", vec, shape, "w");

    // Deserialize using memory mapping
    const bool useMemoryMapping = true;
    const auto loadedData = npypp::LoadFull<double>("arr1.npy", useMemoryMapping);

    assert(loadedData.shape == shape);
    assert(loadedData.data == vec);

    // Compressed archives (.npz)
    npypp::SaveCompressed("arr1.npz", "arr1", vec, shape, "w");
    const auto loadedCompressedData = npypp::LoadCompressedFull<double>("arr1.npz", "arr1");

    assert(loadedCompressedData.shape == shape);
    assert(loadedCompressedData.data == vec);
}
```

### Zero-Copy / Read-Write Memory Mapping
```cpp
#include <Npy++.h>

#include <cassert>
#include <functional>
#include <numeric>
#include <vector>

int main()
{
    const std::vector<size_t> shape { 4, 4, 8 };
    const std::vector<double> vec(128, 0.01);

    const size_t nElements = std::accumulate(shape.begin(), shape.end(), size_t{ 1 }, std::multiplies<size_t>());
    const auto npyHeader = npypp::detail::GetNpyHeader<double>(shape);
    const size_t fileSizeBytes = npyHeader.size() + nElements * sizeof(double);

    // Open the file with read/write memory mapping (size must be known upfront)
    mm::MemoryMappedFile<mm::CacheHint::SequentialScan, mm::MapMode::ReadAndWrite> mmf("arr1_mmap.npy", fileSizeBytes);
    assert(mmf.IsValid());
    npypp::Save(mmf, vec, shape);

    // Rewind mapped cursor before reading back
    mmf.Rewind();

    // Zero-copy std::span view directly into the mapped .npy buffer
    std::vector<size_t> loadedShape;
    const auto spanView = npypp::LoadSpan<double>(mmf, &loadedShape);

    assert(loadedShape == shape);
    assert(spanView.size() == vec.size());
    for (size_t i = 0; i < vec.size(); ++i)
        assert(vec[i] == spanView[i]);
}
```
