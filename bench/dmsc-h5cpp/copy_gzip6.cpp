/* copy_gzip6.cpp — ESS-DMSC h5cpp: element-by-element processing of IEX tick data with gzip level 6.
 *
 * Usage: ./copy-gzip6-dmsc-h5cpp <input.h5> <output.h5>
 *
 * Uses dmsc-h5cpp's Compound type support, reads chunk-aligned blocks,
 * iterates element by element, writes with DEFLATE level 6.
 */

#include <h5cpp/hdf5.hpp>

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

static hdf5::datatype::Compound make_tick_type() {
    auto ctype = hdf5::datatype::Compound::create(sizeof(tick_t));
    ctype.insert("time",        offsetof(tick_t, time),
                 hdf5::datatype::TypeTrait<uint64_t>::create());
    ctype.insert("price",       offsetof(tick_t, price),
                 hdf5::datatype::TypeTrait<float>::create());
    ctype.insert("size",        offsetof(tick_t, size),
                 hdf5::datatype::TypeTrait<uint32_t>::create());
    ctype.insert("contract_id", offsetof(tick_t, contract_id),
                 hdf5::datatype::TypeTrait<uint16_t>::create());
    ctype.insert("flags",       offsetof(tick_t, flags),
                 hdf5::datatype::TypeTrait<uint16_t>::create());
    return ctype;
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
        auto in_file  = hdf5::file::open(in_path, hdf5::file::AccessFlags::ReadOnly);
        auto in_dset  = in_file.root().get_dataset("/" GROUP_NAME "/" DATASET_NAME);
        auto in_dtype = in_dset.datatype();
        auto in_space = in_dset.dataspace();
        auto nelem    = static_cast<size_t>(in_space.size());
        size_t type_size = in_dtype.size();

        printf("Source: %s  elements=%zu  type_size=%zu  total=%.2f GiB  timeout=%ds\n",
               in_path, nelem, type_size,
               static_cast<double>(nelem * type_size) / (1024.0 * 1024.0 * 1024.0),
               TIMEOUT_SEC);

        /* ── Create output ────────────────────────────────────────────────── */
        auto out_file = hdf5::file::create(out_path, hdf5::file::AccessFlags::Truncate);

        hdf5::Dimensions chunk{OUT_CHUNK_ELEMS};
        hdf5::Dimensions dims{nelem};
        auto out_space = hdf5::dataspace::Simple(dims);

        hdf5::property::DatasetCreationList dcpl;
        dcpl.layout(hdf5::property::DatasetLayout::Chunked);
        dcpl.chunk(chunk);
        hdf5::filter::Deflate deflate(6);
        deflate(dcpl, hdf5::filter::Availability::Mandatory);

        auto out_dset = out_file.root().create_dataset(
            "/" GROUP_NAME "/" DATASET_NAME,
            in_dtype, out_space, dcpl);

        /* ── Allocate I/O buffer ──────────────────────────────────────────── */
        const size_t buf_elems = IO_BLOCK_ELEMS;
        std::vector<tick_t> buffer(buf_elems);

        /* ── Big-block copy loop with element-by-element processing ───────── */
        double t0 = monotonic_seconds();
        size_t total_copied = 0;
        size_t next_report  = PROGRESS_EVERY;

        for (size_t offset = 0; offset < nelem && !g_timed_out; offset += buf_elems) {
            size_t count = std::min(buf_elems, nelem - offset);

            hdf5::dataspace::Hyperslab slab({offset}, {count});
            in_dset.read(buffer, slab);

            /* ── element-by-element processing ─────────────────────────────── */
            volatile float checksum = 0.0f;
            for (size_t i = 0; i < count; i++) {
                checksum += buffer[i].price;
            }
            (void)checksum;

            out_dset.write(buffer, slab);

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
