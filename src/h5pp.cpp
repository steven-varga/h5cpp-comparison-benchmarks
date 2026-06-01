// bench-h5pp: h5pp wrapper benchmark.
// SPDX-License-Identifier: MIT

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <h5pp/h5pp.h>
#include <vector>
#include <cstdio>

int main() {
    const char* filename = "bench_h5pp.h5";
    std::remove(filename);

    h5pp::File file(filename, h5pp::FilePermission::REPLACE);
    constexpr size_t N = 1'000'000;
    std::vector<double> data(N, 3.14);

    ankerl::nanobench::Bench bench;
    bench.title("h5pp — write 1M doubles");
    bench.relative(true);

    bench.run("file.writeDataset", [&]() {
        file.writeDataset(data, "data");
    });

    std::remove(filename);
}
