# h5cpp-comparison-benchmarks — Throughput Results

## Dataset

| Property | Value |
|---|---|
| File | `~/scratch/iex.h5` |
| Path | `/irts/2026-05-28` |
| Records | 350,056,387 ticks |
| Type | compound `{time: u64, price: f32, size: u32, contract_id: u16, flags: u16}` = 24 bytes |
| Uncompressed | 7.82 GiB |
| Source compression | none (chunked, 64K elements) |

## Test Setup

All benchmarks do the **identical job**:

1. Open source dataset read-only.
2. Create output dataset with:
   - Chunk size: 4 M elements (~100 MiB)
   - Filter: DEFLATE level 6
3. Copy in 4 M-element blocks (~96 MiB buffer).
4. **Process each tick individually** — touch `price` field to prevent optimization.
5. Report throughput in Mticks/s.
6. Hard cap: 5 minutes (`alarm(300)`).

This measures what each library provides out-of-the-box, including compound-type overhead and per-element processing capability.

## Results

| Benchmark | Time (s) | Mticks/s | MiB/s | Output (GiB) | Ratio |
|---|---|---|---|---|---|
| `bench/capi/copy_gzip6` | **198.2** | **1.77** | 40.4 | 2.30 | 3.41:1 |
| `bench/highfive/copy_gzip6` | **200.1** | **1.75** | 40.0 | 2.30 | 3.41:1 |
| `bench/h5pp/copy_gzip6` | **201.1** | **1.74** | 39.8 | 2.30 | 3.41:1 |

### Notes

- **C API** is the baseline. The element-by-element processing loop (touching each `tick.price`) adds negligible overhead compared to the I/O + compression bottleneck.
- **HighFive** is essentially at parity with the raw C API (~1% difference). Its `CompoundType` registration adds no measurable overhead.
- **h5pp** is also at parity. The workaround for h5pp's `dsetSlab` + `resizeData` interaction (using `buffer.data()` pointer with explicit `dataDims`) is necessary to avoid h5pp resizing the container to the full dataset size before applying the hyperslab.
- All three are **CPU-bound by single-threaded zlib DEFLATE**, not by API or compound-type overhead.
- **dmsc-h5cpp** is not included because the wrapper headers alone are insufficient — the library requires compiled object files (`.cpp` sources were stripped per project policy). Building it from source would be needed to benchmark.

## Concurrent Compression

**None of the libraries tested** expose concurrent / multi-threaded compression natively.

- HDF5's built-in `H5Pset_deflate` routes through a single-threaded zlib backend.
- To get parallel compression today you would need:
  - A custom HDF5 filter using a threaded deflate implementation (e.g. `libdeflate` with external threading), **or**
  - Direct-chunk I/O (`H5Dwrite_chunk` / `H5Dread_chunk`) where the application compresses chunks independently and writes them pre-compressed.

The **vargalabs/h5cpp** direct-chunk pipeline is designed around the latter approach; whether it outperforms the standard path on this workload is what the upcoming `bench-h5cpp` target is meant to measure.

## Build & Run

```bash
cmake -S . -B build
cmake --build build --parallel -t copy-gzip6          # C baseline
cmake --build build --parallel -t copy-gzip6-highfive # HighFive
cmake --build build --parallel -t copy-gzip6-h5pp     # h5pp

./build/copy-gzip6 ~/scratch/iex.h5 /tmp/out_c.h5
./build/copy-gzip6-highfive ~/scratch/iex.h5 /tmp/out_highfive.h5
./build/copy-gzip6-h5pp ~/scratch/iex.h5 /tmp/out_h5pp.h5
```

## TODO

- [x] C API baseline (`bench/capi/copy_gzip6.c`)
- [x] HighFive (`bench/highfive/copy_gzip6.cpp`)
- [x] h5pp (`bench/h5pp/copy_gzip6.cpp`)
- [ ] ESS-DMSC h5cpp — blocked: requires compiled library, not headers-only
- [ ] vargalabs h5cpp (`bench/h5cpp/`)
