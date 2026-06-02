/* copy_gzip6.cpp — h5pp: element-by-element processing of IEX tick data with gzip level 6.
 *
 * Usage: ./copy-gzip6-h5pp <input.h5> <output.h5>
 *
 * Uses h5pp with manual H5T_COMPOUND, reads chunk-aligned blocks,
 * iterates element by element, writes with DEFLATE level 6.
 */

#include <h5pp/h5pp.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <unistd.h>
#include <vector>
#include <cstdint>
#include <algorithm>

#define DATASET_NAME    "2026-05-28"
#define GROUP_NAME      "irts"
#define OUT_CHUNK_ELEMS 4194304ULL   /* 4 M elements  ≈ 100 MiB per chunk */
#define IO_BLOCK_ELEMS  4194304ULL   /* one chunk at a time */
#define TIMEOUT_SEC     300
#define PROGRESS_EVERY  100000000ULL

struct tick_t {
    uint64_t time;
    float    price;
    uint32_t size;
    uint16_t contract_id;
    uint16_t flags;
};

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

static h5pp::hid::h5t make_tick_type() {
    h5pp::hid::h5t t = H5Tcreate(H5T_COMPOUND, sizeof(tick_t));
    H5Tinsert(t, "time",        HOFFSET(tick_t, time),        H5T_NATIVE_UINT64);
    H5Tinsert(t, "price",       HOFFSET(tick_t, price),       H5T_NATIVE_FLOAT);
    H5Tinsert(t, "size",        HOFFSET(tick_t, size),        H5T_NATIVE_UINT32);
    H5Tinsert(t, "contract_id", HOFFSET(tick_t, contract_id), H5T_NATIVE_UINT16);
    H5Tinsert(t, "flags",       HOFFSET(tick_t, flags),       H5T_NATIVE_UINT16);
    return t;
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
        h5pp::File in_file(in_path, h5pp::FileAccess::READONLY);
        auto in_info = in_file.getDatasetInfo("/" GROUP_NAME "/" DATASET_NAME);
        size_t nelem = 1;
        for (auto d : in_info.dsetDims.value()) nelem *= d;
        size_t type_size = H5Tget_size(in_info.h5Type.value());

        printf("Source: %s  elements=%zu  type_size=%zu  total=%.2f GiB  timeout=%ds\n",
               in_path, nelem, type_size,
               static_cast<double>(nelem * type_size) / (1024.0 * 1024.0 * 1024.0),
               TIMEOUT_SEC);

        /* ── Create output ────────────────────────────────────────────────── */
        h5pp::File out_file(out_path, h5pp::FileAccess::REPLACE);
        auto tick_dtype = make_tick_type();
        out_file.createDataset("/" GROUP_NAME "/" DATASET_NAME,
                               tick_dtype,
                               H5D_CHUNKED,
                               std::vector<hsize_t>{nelem},
                               std::vector<hsize_t>{OUT_CHUNK_ELEMS},
                               std::nullopt,
                               6);

        /* ── Allocate I/O buffer ──────────────────────────────────────────── */
        const size_t buf_elems = IO_BLOCK_ELEMS;
        std::vector<tick_t> buffer(buf_elems);

        /* ── Big-block copy loop with element-by-element processing ───────── */
        double t0 = monotonic_seconds();
        size_t total_copied = 0;
        size_t next_report  = PROGRESS_EVERY;

        for (size_t offset = 0; offset < nelem && !g_timed_out; offset += buf_elems) {
            size_t count = std::min(buf_elems, nelem - offset);

            h5pp::Hyperslab slab({offset}, {count});

            h5pp::Options read_opts;
            read_opts.linkPath = "/" GROUP_NAME "/" DATASET_NAME;
            read_opts.dsetSlab = slab;
            read_opts.dataDims = std::vector<hsize_t>{count};
            read_opts.h5Type   = tick_dtype;
            tick_t* read_ptr = buffer.data();
            in_file.readDataset(read_ptr, read_opts);

            /* ── element-by-element processing ─────────────────────────────── */
            volatile float checksum = 0.0f;
            for (size_t i = 0; i < count; i++) {
                checksum += buffer[i].price;
            }
            (void)checksum;

            h5pp::Options write_opts;
            write_opts.linkPath = "/" GROUP_NAME "/" DATASET_NAME;
            write_opts.dsetSlab = slab;
            write_opts.dataDims = std::vector<hsize_t>{count};
            write_opts.h5Type   = tick_dtype;
            tick_t* write_ptr = buffer.data();
            out_file.writeDataset(write_ptr, write_opts);

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
