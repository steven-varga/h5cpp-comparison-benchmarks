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
3. Copy in 50 M-element blocks (~1.2 GiB buffer).
4. Report throughput in Mticks/s.
5. Hard cap: 5 minutes (`alarm(300)`).

This measures what each library provides out-of-the-box. No custom filter pipelines, no threading hacks, no bypassing the library's standard path.

## Results

| Benchmark | Time (s) | Mticks/s | MiB/s | Output (GiB) | Ratio |
|---|---|---|---|---|---|
| `bench/capi/copy_gzip6` | ~210 | **1.67** | ~38.1 | 2.30 | 3.41:1 |
| `bench/highfive/copy_gzip6` | 213.8 | **1.64** | 37.5 | 2.30 | 3.41:1 |

### Notes

- **HighFive** is essentially at parity with the raw C API (~2% difference is run-to-run noise). This is expected: it is a thin header-only wrapper around `H5Dread`/`H5Dwrite`.
- The bottleneck is **single-threaded zlib DEFLATE** inside HDF5, not I/O bandwidth or API overhead.
- Chunk-size experiments (64K → 4M) on the C baseline showed negligible improvement (~1.69 vs 1.78 Mticks/s), confirming the CPU-bound nature of gzip.

## Concurrent Compression

**None of the libraries tested so far** (raw C API, HighFive) expose concurrent / multi-threaded compression natively.

- HDF5's built-in `H5Pset_deflate` routes through a single-threaded zlib backend.
- To get parallel compression today you would need:
  - A custom HDF5 filter using a threaded deflate implementation (e.g. `libdeflate` with external threading, or `pigz`-style chunk parallelism), **or**
  - Direct-chunk I/O (`H5Dwrite_chunk` / `H5Dread_chunk`) where the application compresses chunks independently and writes them pre-compressed.

The **vargalabs/h5cpp** direct-chunk pipeline is designed around the latter approach; whether it outperforms the standard path on this workload is exactly what the upcoming `bench-h5cpp` target is meant to measure.

## Build & Run

```bash
cmake -S . -B build
cmake --build build --parallel -t copy-gzip6          # C baseline
cmake --build build --parallel -t copy-gzip6-highfive # HighFive

./build/copy-gzip6 ~/scratch/iex.h5 /tmp/out_c.h5
./build/copy-gzip6-highfive ~/scratch/iex.h5 /tmp/out_highfive.h5
```

## TODO

- [x] C API baseline (`bench/capi/copy_gzip6.c`)
- [x] HighFive (`bench/highfive/copy_gzip6.cpp`)
- [ ] h5pp (`bench/h5pp/`)
- [ ] ESS-DMSC h5cpp (`bench/dmsc-h5cpp/`)
- [ ] vargalabs h5cpp (`bench/h5cpp/`)
