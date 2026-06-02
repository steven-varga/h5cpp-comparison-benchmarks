# h5cpp-comparison-benchmarks

Cross-library HDF5 C++ benchmark suite.

This project compares the performance and API ergonomics of popular C++ HDF5
wrappers against the raw HDF5 C API. All benchmarks target **Linux only** and
require a C++20-capable compiler.

---

## Prerequisites

### 1. Install HDF5 v2.1.1

The benchmark suite is designed to run against **HDF5 2.1.1** (released
2026-03-23). Install it from source or use a pre-built binary from
[The HDF Group](https://www.hdfgroup.org/download-hdf5/):

```bash
wget https://github.com/HDFGroup/hdf5/releases/download/hdf5_2.1.1/hdf5-2.1.1.tar.gz
tar -xzf hdf5-2.1.1.tar.gz
cd hdf5-2.1.1
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local/HDF_Group/HDF5/2.1.1 \
         -DHDF5_BUILD_CPP_LIB=OFF \
         -DBUILD_TESTING=OFF \
         -DHDF5_BUILD_EXAMPLES=OFF
make -j$(nproc)
sudo make install
```

Then expose it to CMake:

```bash
export HDF5_ROOT=/usr/local/HDF_Group/HDF5/2.1.1
```

### 2. Build the Benchmark Suite

```bash
cmake -S . -B build -DHDF5_ROOT=$HDF5_ROOT
cmake --build build --parallel
```

Binaries are emitted into `build/bench-*`.

---

## Obtaining the Benchmark Dataset

The benchmark operates on a **real-world financial time-series dataset** from
the Investors Exchange (IEX). We use two open-source tools developed by
VargaLabs to download and convert the raw market data into HDF5:

| Tool | Repository | Purpose |
|---|---|---|
| **iex-download** | https://github.com/vargalabs/iex-download | Download IEX TOPS pcap feeds |
| **iex2h5** | https://github.com/vargalabs/iex2h5 | Convert pcap feeds into HDF5 |

### Step 1 — Download a trading day

```bash
iex-download --tops --directory ./ 2026-05-28
```

This produces `TOPS-2026-05-28.pcap.gz` (~8.9 GiB compressed).

### Step 2 — Convert to HDF5

```bash
iex2h5 -o ./iex.h5 -c irts --gzip 0 ./TOPS-2026-05-28.pcap.gz
```

Expected output:

```
[iex2h5] Converting 1 file using backend: hdf5 — using 1 thread — © Varga Consulting, 2017–2025
▫ 2026-05-28 14:30:00 21:00:00 ✓
benchmark: 350056387 events in 57506ms  6.1 Mticks/s, 0.164000 µs/tick latency, 8.90 GiB input converted into 7.83 GiB output
[iex2h5] Conversion complete — all files processed successfully
```

### Resulting Dataset Structure

```
$ h5ls -r iex.h5
/                        Group
/instruments.txt         Dataset {11693/65536}
/irts                    Group
/irts/2026-05-28         Dataset {350056387/Inf}
/stats                   Group
/stats/2026-05-28        Group
/stats/2026-05-28/event_count Dataset {11693}
/stats/2026-05-28/trade_count Dataset {11693}
/stats/2026-05-28/trade_size Dataset {11693}
/trading_days.txt        Dataset {1/Inf}
```

| Path | Description |
|---|---|
| `/irts/2026-05-28` | **350M+ tick events** — the primary benchmark payload |
| `/instruments.txt` | Symbol registry (11,693 instruments) |
| `/stats/2026-05-28/*` | Per-instrument aggregates (event count, trade count, trade size) |
| `/trading_days.txt` | Metadata index |

The output file `iex.h5` is **~7.9 GiB**.

> ⚠️ **Do not commit `iex.h5` or `*.pcap.gz` to this repository.** GitHub blocks
> files larger than 100 MiB. The benchmark binaries generate or read this file
> locally; it is not tracked in git.

---

## Wrapper Libraries Under Test

| Wrapper | Type | Source |
|---|---|---|
| Raw HDF5 C API | Baseline | System install |
| **HighFive** | Header-only, C++14 | `wrappers/highfive/` |
| **ESS-DMSC h5cpp** | Compiled, C++11/14 | `wrappers/dmsc-h5cpp/` |
| **h5pp** | Header-only, C++17 | `wrappers/h5pp/` |
| **h5cpp (VargaLabs)** | Header-only, C++20 | `wrappers/vargalabs/` |

Only headers and licences are vendored in-tree; build artefacts and upstream
source files are excluded.

---

## Running Benchmarks

```bash
# Run all available benchmarks
./build/bench-hdf5-c
./build/bench-highfive
./build/bench-h5cpp
# etc.
```

Each binary reports nanobench-formatted throughput and latency numbers.

---

## Licence

MIT — see individual `LICENSE` / `COPYING` files in `thirdparty/` and
`wrappers/` for upstream terms.
