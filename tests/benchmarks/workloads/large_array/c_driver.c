/* Verifies: REQ-PF-001
 *
 * Throughput driver for the LargeArray workload, zserio-c side.
 * 262144 uint32 elements; encoded ~ 1 MB plus varsize prefix.
 *
 * The encode and decode buffers are heap-allocated once outside the
 * timed loop so per-iteration cost reflects the codec work, not the
 * allocator. */

#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "benchmarks/large_array/large_array.h"

#define ELEMENT_COUNT 262144U
#define BUF_BYTES (ELEMENT_COUNT * 4U + 16U)

static double ns_diff(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) * 1e9
         + (double)(b.tv_nsec - a.tv_nsec);
}

int main(int argc, char *argv[])
{
    const int N = (argc > 1) ? atoi(argv[1]) : 200;
    if (N <= 0) { fprintf(stderr, "usage: %s [n_iters]\n", argv[0]); return 2; }

    benchmarks_large_array_large_array_t v;
    benchmarks_large_array_large_array_init(&v);
    v.data_count = ELEMENT_COUNT;
    v.data = (uint32_t *)calloc(ELEMENT_COUNT, sizeof(uint32_t));
    if (v.data == NULL) { fprintf(stderr, "alloc\n"); return 1; }
    /* Deterministic fill so encode + decode work is consistent. */
    uint32_t s = 0xCAFEBABEU;
    for (size_t i = 0U; i < ELEMENT_COUNT; ++i) {
        s = s * 1103515245U + 12345U;
        v.data[i] = s;
    }

    uint8_t *buf = (uint8_t *)malloc(BUF_BYTES);
    if (buf == NULL) { fprintf(stderr, "buf alloc\n"); free(v.data); return 1; }

    /* Warm-up + size discovery. */
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, (size_t)BUF_BYTES * 8U);
    if (benchmarks_large_array_large_array_write(&v, &w) != ZSERIO_OK) {
        fprintf(stderr, "warm-up write failed\n");
        free(v.data); free(buf); return 1;
    }
    const size_t bytes = (zserio_bitstream_writer_position(&w) + 7U) / 8U;
    const size_t bits  = zserio_bitstream_writer_position(&w);

    /* Encode timing. */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) {
        zserio_bitstream_writer_init(&w, buf, (size_t)BUF_BYTES * 8U);
        (void)benchmarks_large_array_large_array_write(&v, &w);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double encode_ns = ns_diff(t0, t1) / (double)N;

    /* Decode timing. The runtime allocates a fresh data array per
     * decode; that allocation cost shows up in the number but
     * matches what the cpp counterpart does. The sink touches a
     * fixed-index element so the optimiser can't DCE the read. */
    const zserio_allocator_t alloc = zserio_allocator_libc();
    uint64_t sink = 0U;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) {
        zserio_bitstream_reader_t r;
        zserio_bitstream_reader_init(&r, buf, bits);
        benchmarks_large_array_large_array_t out;
        benchmarks_large_array_large_array_init(&out);
        (void)benchmarks_large_array_large_array_read(&out, &r, &alloc);
        if (out.data != NULL && out.data_count > 0U) {
            sink ^= (uint64_t)out.data[0] ^ (uint64_t)out.data[out.data_count - 1U];
        }
        benchmarks_large_array_large_array_free(&out, &alloc);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double decode_ns = ns_diff(t0, t1) / (double)N;

    fprintf(stdout, "encode_ns_per_op=%.2f decode_ns_per_op=%.2f bytes=%zu sink=%llu\n",
            encode_ns, decode_ns, bytes, (unsigned long long)sink);
    free(v.data); free(buf);
    return 0;
}
