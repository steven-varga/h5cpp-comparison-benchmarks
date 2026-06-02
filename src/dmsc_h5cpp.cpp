// bench-dmsc-h5cpp: ESS-DMSC h5cpp wrapper benchmark.
// SPDX-License-Identifier: MIT

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <h5cpp/hdf5.hpp>
#include <vector>
#include <cstdio>

int main() {
    const char* filename = "bench_dmsc_h5cpp.h5";
    std::remove(filename);

    auto file = hdf5::file::create(filename, hdf5::file::AccessFlags::Truncate);
    auto root = file.root();

    constexpr size_t N = 1'000'000;
    std::vector<double> data(N, 3.14);

    auto type = hdf5::datatype::create<std::vector<double>>();
    auto space = hdf5::dataspace::create(data);
    auto dataset = root.create_dataset("data", type, space);

    ankerl::nanobench::Bench bench;
    bench.title("ESS-DMSC h5cpp — write 1M doubles");
    bench.relative(true);

    bench.run("dataset.write", [&]() {
        dataset.write(data);
    });

    std::remove(filename);
}
