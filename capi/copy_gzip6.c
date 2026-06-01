/* copy_gzip6.c — Read IEX tick data efficiently and rewrite with gzip level 6.
 *
 * Usage: ./copy-gzip6 <input.h5> <output.h5>
 *
 * Hard cap: 5 minutes (exits gracefully with partial result if exceeded).
 */

#include <hdf5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#define DATASET_NAME   "2026-05-28"
#define GROUP_NAME     "irts"
#define CHUNK_ELEMENTS 65536
#define TIMEOUT_SEC    300          /* 5 minutes */
#define PROGRESS_EVERY 100000000ULL /* print every ~100M ticks */

static volatile int g_timed_out = 0;

static void on_alarm(int sig) {
    (void)sig;
    g_timed_out = 1;
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.h5> <output.h5>\n", argv[0]);
        return 1;
    }

    const char *in_path  = argv[1];
    const char *out_path = argv[2];

    signal(SIGALRM, on_alarm);
    alarm(TIMEOUT_SEC);

    hid_t in_file   = -1;
    hid_t in_dset   = -1;
    hid_t in_dspace = -1;
    hid_t in_dtype  = -1;
    hid_t in_dcpl   = -1;

    hid_t out_file   = -1;
    hid_t out_grp    = -1;
    hid_t out_dcpl   = -1;
    hid_t out_dspace = -1;
    hid_t out_dset   = -1;
    hid_t mem_dspace = -1;

    void *buffer = NULL;
    int rc = 0;

    /* ── Open source ──────────────────────────────────────────────────── */
    in_file = H5Fopen(in_path, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (in_file < 0) { fprintf(stderr, "Failed to open input: %s\n", in_path); rc = 1; goto cleanup; }

    in_dset = H5Dopen2(in_file, "/" GROUP_NAME "/" DATASET_NAME, H5P_DEFAULT);
    if (in_dset < 0) { fprintf(stderr, "Failed to open dataset\n"); rc = 1; goto cleanup; }

    in_dspace = H5Dget_space(in_dset);
    in_dtype  = H5Dget_type(in_dset);
    in_dcpl   = H5Dget_create_plist(in_dset);

    hsize_t dims[1];
    H5Sget_simple_extent_dims(in_dspace, dims, NULL);
    const hsize_t nelements = dims[0];
    const size_t  type_size = H5Tget_size(in_dtype);

    printf("Source: %s  elements=%llu  type_size=%zu  total=%.2f GiB  timeout=%ds\n",
           in_path, (unsigned long long)nelements, type_size,
           (double)(nelements * type_size) / (1024.0 * 1024.0 * 1024.0),
           TIMEOUT_SEC);

    /* ── Create output ────────────────────────────────────────────────── */
    out_file = H5Fcreate(out_path, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (out_file < 0) { fprintf(stderr, "Failed to create output\n"); rc = 1; goto cleanup; }

    out_grp = H5Gcreate2(out_file, GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (out_grp < 0) { fprintf(stderr, "Failed to create group\n"); rc = 1; goto cleanup; }

    hsize_t chunk_dims[1] = { CHUNK_ELEMENTS };
    if (H5Pget_chunk(in_dcpl, 1, chunk_dims) < 0) {
        chunk_dims[0] = CHUNK_ELEMENTS;
    }

    out_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5Pset_chunk(out_dcpl, 1, chunk_dims);
    H5Pset_deflate(out_dcpl, 6); /* gzip level 6 */

    out_dspace = H5Screate_simple(1, &nelements, NULL);
    out_dset = H5Dcreate2(out_file, "/" GROUP_NAME "/" DATASET_NAME,
                          in_dtype, out_dspace,
                          H5P_DEFAULT, out_dcpl, H5P_DEFAULT);
    if (out_dset < 0) { fprintf(stderr, "Failed to create output dataset\n"); rc = 1; goto cleanup; }

    /* ── Buffer ───────────────────────────────────────────────────────── */
    const hsize_t buf_elements = chunk_dims[0];
    buffer = aligned_alloc(64, (size_t)buf_elements * type_size);
    if (!buffer) { fprintf(stderr, "Failed to allocate buffer\n"); rc = 1; goto cleanup; }

    mem_dspace = H5Screate_simple(1, &buf_elements, NULL);

    /* ── Chunked copy with progress + timeout ─────────────────────────── */
    double t0 = monotonic_seconds();
    hsize_t total_copied = 0;
    hsize_t next_report  = PROGRESS_EVERY;

    for (hsize_t offset = 0; offset < nelements && !g_timed_out; offset += buf_elements) {
        hsize_t count = buf_elements;
        if (offset + count > nelements)
            count = nelements - offset;

        H5Sselect_hyperslab(in_dspace, H5S_SELECT_SET, &offset, NULL, &count, NULL);
        if (count != buf_elements)
            H5Sset_extent_simple(mem_dspace, 1, &count, NULL);

        if (H5Dread(in_dset, in_dtype, mem_dspace, in_dspace, H5P_DEFAULT, buffer) < 0) {
            fprintf(stderr, "Read failed at offset %llu\n", (unsigned long long)offset);
            rc = 1; break;
        }

        hid_t out_file_dspace = H5Dget_space(out_dset);
        H5Sselect_hyperslab(out_file_dspace, H5S_SELECT_SET, &offset, NULL, &count, NULL);

        if (H5Dwrite(out_dset, in_dtype, mem_dspace, out_file_dspace, H5P_DEFAULT, buffer) < 0) {
            fprintf(stderr, "Write failed at offset %llu\n", (unsigned long long)offset);
            H5Sclose(out_file_dspace);
            rc = 1; break;
        }
        H5Sclose(out_file_dspace);

        total_copied += count;

        if (total_copied >= next_report) {
            double elapsed = monotonic_seconds() - t0;
            printf("  progress: %llu ticks  %.1f s  %.2f Mticks/s\n",
                   (unsigned long long)total_copied, elapsed,
                   (total_copied / elapsed) / 1e6);
            next_report += PROGRESS_EVERY;
        }
    }

    double elapsed = monotonic_seconds() - t0;
    double gib_processed = (double)(total_copied * type_size) / (1024.0 * 1024.0 * 1024.0);

    if (g_timed_out) {
        printf("TIMEOUT after %.1f s — partial copy: %llu ticks (%.3f GiB)\n",
               elapsed, (unsigned long long)total_copied, gib_processed);
        rc = 2;
    } else {
        printf("Copied %llu ticks in %.3f s  %.3f GiB  %.2f MiB/s  %.2f Mticks/s\n",
               (unsigned long long)total_copied, elapsed, gib_processed,
               (gib_processed * 1024.0) / elapsed,
               (total_copied / elapsed) / 1e6);
    }

    /* ── Cleanup ──────────────────────────────────────────────────────── */
cleanup:
    if (buffer)      free(buffer);
    if (mem_dspace >= 0) H5Sclose(mem_dspace);
    if (out_dset   >= 0) H5Dclose(out_dset);
    if (out_dspace >= 0) H5Sclose(out_dspace);
    if (out_dcpl   >= 0) H5Pclose(out_dcpl);
    if (out_grp    >= 0) H5Gclose(out_grp);
    if (out_file   >= 0) H5Fclose(out_file);

    if (in_dspace  >= 0) H5Sclose(in_dspace);
    if (in_dtype   >= 0) H5Tclose(in_dtype);
    if (in_dcpl    >= 0) H5Pclose(in_dcpl);
    if (in_dset    >= 0) H5Dclose(in_dset);
    if (in_file    >= 0) H5Fclose(in_file);

    return rc;
}
