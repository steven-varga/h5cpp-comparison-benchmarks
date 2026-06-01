// bench-hdf5-c: Raw HDF5 C API baseline benchmark.
// SPDX-License-Identifier: MIT

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <hdf5.h>
#include <vector>
#include <cstdio>

int main() {
    const char* filename = "bench_hdf5_c.h5";
    std::remove(filename);

    hid_t file = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0) return 1;

    constexpr size_t N = 1'000'000;
    std::vector<double> data(N, 3.14);

    hsize_t dims[1] = {N};
    hid_t dataspace = H5Screate_simple(1, dims, nullptr);
    hid_t dataset = H5Dcreate2(file, "data", H5T_NATIVE_DOUBLE, dataspace,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    ankerl::nanobench::Bench bench;
    bench.title("Raw HDF5 C API — write 1M doubles");
    bench.relative(true);

    bench.run("H5Dwrite", [&]() {
        H5Dwrite(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());
    });

    H5Dclose(dataset);
    H5Sclose(dataspace);
    H5Fclose(file);
    std::remove(filename);
}
