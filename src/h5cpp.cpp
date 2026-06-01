// bench-h5cpp: Steven Varga h5cpp wrapper benchmark.
// SPDX-License-Identifier: MIT
// Added manually per project owner instruction.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

// #include <h5cpp/...>
#include <vector>
#include <cstdio>

int main() {
    const char* filename = "bench_h5cpp.h5";
    std::remove(filename);

    // TODO: add h5cpp benchmark implementation

    return 0;
}
