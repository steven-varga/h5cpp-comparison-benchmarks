// bench-highfive: HighFive wrapper benchmark.
// SPDX-License-Identifier: MIT

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <highfive/highfive.hpp>
#include <vector>
#include <cstdio>

int main() {
    const char* filename = "bench_highfive.h5";
    std::remove(filename);

    HighFive::File file(filename, HighFive::File::Truncate);
    constexpr size_t N = 1'000'000;
    std::vector<double> data(N, 3.14);

    auto dataset = file.createDataSet<double>("data", HighFive::DataSpace::From(data));

    ankerl::nanobench::Bench bench;
    bench.title("HighFive — write 1M doubles");
    bench.relative(true);

    bench.run("dataset.write", [&]() {
        dataset.write(data);
    });

    std::remove(filename);
}
