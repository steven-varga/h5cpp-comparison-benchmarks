// bench-h5cpp: Steven Varga h5cpp wrapper benchmark.
// SPDX-License-Identifier: MIT
// Added manually per project owner instruction.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <h5cpp/H5Fcreate.hpp>
#include <h5cpp/H5Dcreate.hpp>
#include <h5cpp/H5Dwrite.hpp>

#include <vector>
#include <cstdio>

int main() {
    const char* filename = "bench_h5cpp.h5";
    std::remove(filename);

    h5::fd_t fd = h5::create(filename, H5F_ACC_TRUNC);
    constexpr size_t N = 1'000'000;
    std::vector<double> data(N, 3.14);

    auto ds = h5::create<double>(fd, "data", h5::count{N});

    ankerl::nanobench::Bench bench;
    bench.title("h5cpp — write 1M doubles");
    bench.relative(true);

    bench.run("h5::write", [&]() {
        h5::write(fd, "data", data);
    });

    std::remove(filename);
}
