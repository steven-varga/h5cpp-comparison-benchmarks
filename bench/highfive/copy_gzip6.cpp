/* copy_gzip6.cpp — HighFive wrapper: chunked copy of IEX tick data with gzip level 6.
 *
 * Usage: ./copy-gzip6-highfive <input.h5> <output.h5>
 *
 * Identical job to bench/capi/copy_gzip6.c: read chunk-aligned blocks from source,
 * write to output with DEFLATE level 6 and 4 M-element chunks.
 */

#include <highfive/highfive.hpp>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <unistd.h>
#include <vector>

#define DATASET_NAME    "2026-05-28"
#define GROUP_NAME      "irts"
#define OUT_CHUNK_ELEMS 4194304ULL   /* 4 M elements  ≈ 100 MiB per chunk */
#define IO_BLOCK_ELEMS  50000000ULL  /* 50 M elements ≈ 1.2 GiB per read/write */
#define TIMEOUT_SEC     300
#define PROGRESS_EVERY  100000000ULL

static volatile int g_timed_out = 0;

static void on_alarm(int sig) {
    (void)sig;
    g_timed_out = 1;
}

static double monotonic_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.h5> <output.h5>\n", argv[0]);
        return 1;
    }

    const char* in_path  = argv[1];
    const char* out_path = argv[2];

    signal(SIGALRM, on_alarm);
    alarm(TIMEOUT_SEC);
    setbuf(stdout, NULL);

    try {
        /* ── Open source ──────────────────────────────────────────────────── */
        HighFive::File in_file(in_path, HighFive::File::ReadOnly);
        auto in_dset = in_file.getDataSet("/" GROUP_NAME "/" DATASET_NAME);

        auto dtype     = in_dset.getDataType();
        auto nelements = in_dset.getElementCount();
        size_t type_size = dtype.getSize();

        printf("Source: %s  elements=%zu  type_size=%zu  total=%.2f GiB  timeout=%ds\n",
               in_path, nelements, type_size,
               static_cast<double>(nelements * type_size) / (1024.0 * 1024.0 * 1024.0),
               TIMEOUT_SEC);

        /* ── Create output ────────────────────────────────────────────────── */
        HighFive::File out_file(out_path, HighFive::File::Truncate);

        HighFive::DataSetCreateProps createProps;
        createProps.add(HighFive::Chunking(std::vector<hsize_t>{OUT_CHUNK_ELEMS}));
        createProps.add(HighFive::Deflate(6));

        HighFive::DataSpace space(std::vector<size_t>{nelements});
        auto out_dset = out_file.createDataSet(
            "/" GROUP_NAME "/" DATASET_NAME,
            space, dtype, createProps, HighFive::DataSetAccessProps::Default(), true);

        /* ── Allocate I/O buffer ──────────────────────────────────────────── */
        const size_t buf_elems = IO_BLOCK_ELEMS;
        std::vector<char> buffer(buf_elems * type_size);

        /* ── Big-block copy loop ──────────────────────────────────────────── */
        double t0 = monotonic_seconds();
        size_t total_copied = 0;
        size_t next_report  = PROGRESS_EVERY;

        for (size_t offset = 0; offset < nelements && !g_timed_out; offset += buf_elems) {
            size_t count = buf_elems;
            if (offset + count > nelements)
                count = nelements - offset;

            in_dset.select(std::vector<size_t>{offset}, std::vector<size_t>{count})
                   .read_raw(buffer.data(), dtype);

            out_dset.select(std::vector<size_t>{offset}, std::vector<size_t>{count})
                    .write_raw(buffer.data(), dtype);

            total_copied += count;
            if (total_copied >= next_report) {
                double el = monotonic_seconds() - t0;
                printf("  progress: %zu ticks  %.1f s  %.2f Mticks/s\n",
                       total_copied, el, (total_copied / el) / 1e6);
                next_report += PROGRESS_EVERY;
            }
        }

        double elapsed = monotonic_seconds() - t0;
        double gib = static_cast<double>(total_copied * type_size) / (1024.0 * 1024.0 * 1024.0);

        if (g_timed_out) {
            printf("TIMEOUT after %.1f s — partial: %zu ticks (%.3f GiB)\n",
                   elapsed, total_copied, gib);
            return 2;
        } else {
            printf("Done: %zu ticks in %.3f s  %.3f GiB  %.2f MiB/s  %.2f Mticks/s\n",
                   total_copied, elapsed, gib,
                   (gib * 1024.0) / elapsed, (total_copied / elapsed) / 1e6);
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
