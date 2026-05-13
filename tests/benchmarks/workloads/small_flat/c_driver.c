/* Verifies: REQ-PF-001
 *
 * Throughput driver for the SmallFlat workload, zserio-c side.
 *
 * Times N inner iterations of encode-from-value and decode-from-bytes,
 * then prints one line:
 *     encode_ns_per_op=<x> decode_ns_per_op=<y> bytes=<z>
 *
 * The harness runs this driver multiple times and takes the median
 * to suppress noise. */

#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "benchmarks/small_flat/small_flat.h"

static double ns_diff(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) * 1e9
         + (double)(b.tv_nsec - a.tv_nsec);
}

int main(int argc, char *argv[])
{
    const int N = (argc > 1) ? atoi(argv[1]) : 200000;
    if (N <= 0) { fprintf(stderr, "usage: %s [n_iters]\n", argv[0]); return 2; }

    benchmarks_small_flat_small_flat_t v;
    benchmarks_small_flat_small_flat_init(&v);
    v.a = 0xABU;
    v.b = 0x1234U;
    v.c = 0x12345678U;
    v.d = UINT64_C(0x0123456789ABCDEF);
    v.e = -42;
    v.f = -12345;
    v.g = -1234567;
    v.h = INT64_C(-1234567890123);
    v.i = true;
    v.j = 0x0BU;

    uint8_t buf[64];
    /* Warm-up + size discovery. */
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8U);
    if (benchmarks_small_flat_small_flat_write(&v, &w) != ZSERIO_OK) {
        fprintf(stderr, "warm-up write failed\n"); return 1;
    }
    const size_t bytes = (zserio_bitstream_writer_position(&w) + 7U) / 8U;
    const size_t bits  = zserio_bitstream_writer_position(&w);

    /* Encode timing. */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) {
        zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8U);
        (void)benchmarks_small_flat_small_flat_write(&v, &w);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double encode_ns = ns_diff(t0, t1) / (double)N;

    /* Decode timing. Flat struct never allocates, but the read API
     * takes the allocator for uniformity with variable-size types. */
    const zserio_allocator_t alloc = zserio_allocator_libc();
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) {
        zserio_bitstream_reader_t r;
        zserio_bitstream_reader_init(&r, buf, bits);
        benchmarks_small_flat_small_flat_t out;
        benchmarks_small_flat_small_flat_init(&out);
        (void)benchmarks_small_flat_small_flat_read(&out, &r, &alloc);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double decode_ns = ns_diff(t0, t1) / (double)N;

    fprintf(stdout, "encode_ns_per_op=%.2f decode_ns_per_op=%.2f bytes=%zu\n",
            encode_ns, decode_ns, bytes);
    return 0;
}
