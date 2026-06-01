/* copy_gzip9.c — Read IEX tick data efficiently and rewrite with gzip level 9.
 *
 * Usage: ./copy_gzip9 <input.h5> <output.h5>
 *
 * Reads the /irts/YYYY-MM-DD dataset in chunk-aligned blocks and writes
 * an identical dataset with DEFLATE level 9 applied.
 */

#include <hdf5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DATASET_NAME   "2026-05-28"
#define GROUP_NAME     "irts"
#define CHUNK_ELEMENTS 65536

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

    /* ── Open source file and dataset ─────────────────────────────────── */
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

    printf("Source: %s  elements=%llu  type_size=%zu  total=%.2f GiB\n",
           in_path, (unsigned long long)nelements, type_size,
           (double)(nelements * type_size) / (1024.0 * 1024.0 * 1024.0));

    /* ── Create output file and group ─────────────────────────────────── */
    out_file = H5Fcreate(out_path, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (out_file < 0) { fprintf(stderr, "Failed to create output: %s\n", out_path); rc = 1; goto cleanup; }

    out_grp = H5Gcreate2(out_file, GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (out_grp < 0) { fprintf(stderr, "Failed to create output group\n"); rc = 1; goto cleanup; }

    /* Re-use source chunk dimensions for aligned I/O */
    hsize_t chunk_dims[1] = { CHUNK_ELEMENTS };
    if (H5Pget_chunk(in_dcpl, 1, chunk_dims) < 0) {
        chunk_dims[0] = CHUNK_ELEMENTS;
    }

    out_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5Pset_chunk(out_dcpl, 1, chunk_dims);
    H5Pset_deflate(out_dcpl, 9); /* gzip level 9 */

    out_dspace = H5Screate_simple(1, &nelements, NULL);
    out_dset = H5Dcreate2(out_file, "/" GROUP_NAME "/" DATASET_NAME,
                          in_dtype, out_dspace,
                          H5P_DEFAULT, out_dcpl, H5P_DEFAULT);
    if (out_dset < 0) { fprintf(stderr, "Failed to create output dataset\n"); rc = 1; goto cleanup; }

    /* ── Allocate chunk-aligned buffer ────────────────────────────────── */
    const hsize_t buf_elements = chunk_dims[0];
    buffer = aligned_alloc(64, (size_t)buf_elements * type_size);
    if (!buffer) { fprintf(stderr, "Failed to allocate buffer\n"); rc = 1; goto cleanup; }

    /* ── Chunked copy loop ────────────────────────────────────────────── */
    mem_dspace = H5Screate_simple(1, &buf_elements, NULL);

    double t0 = monotonic_seconds();
    hsize_t total_copied = 0;

    for (hsize_t offset = 0; offset < nelements; offset += buf_elements) {
        hsize_t count = buf_elements;
        if (offset + count > nelements)
            count = nelements - offset;

        /* select source hyperslab */
        H5Sselect_hyperslab(in_dspace, H5S_SELECT_SET, &offset, NULL, &count, NULL);

        /* shrink memory dataspace for final partial chunk */
        if (count != buf_elements) {
            H5Sset_extent_simple(mem_dspace, 1, &count, NULL);
        }

        /* read */
        if (H5Dread(in_dset, in_dtype, mem_dspace, in_dspace, H5P_DEFAULT, buffer) < 0) {
            fprintf(stderr, "Read failed at offset %llu\n", (unsigned long long)offset);
            rc = 1;
            break;
        }

        /* write */
        hid_t out_file_dspace = H5Dget_space(out_dset);
        H5Sselect_hyperslab(out_file_dspace, H5S_SELECT_SET, &offset, NULL, &count, NULL);

        if (H5Dwrite(out_dset, in_dtype, mem_dspace, out_file_dspace, H5P_DEFAULT, buffer) < 0) {
            fprintf(stderr, "Write failed at offset %llu\n", (unsigned long long)offset);
            H5Sclose(out_file_dspace);
            rc = 1;
            break;
        }
        H5Sclose(out_file_dspace);

        total_copied += count;
    }

    double elapsed = monotonic_seconds() - t0;
    double gib_processed = (double)(total_copied * type_size) / (1024.0 * 1024.0 * 1024.0);

    printf("Copied %llu elements in %.3f s  (%.3f GiB  %.2f MiB/s)\n",
           (unsigned long long)total_copied, elapsed, gib_processed,
           (gib_processed * 1024.0) / elapsed);

    /* ── Single exit-point cleanup ────────────────────────────────────── */
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
