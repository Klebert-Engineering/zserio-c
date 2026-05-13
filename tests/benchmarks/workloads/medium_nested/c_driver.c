/* Verifies: REQ-PF-001
 *
 * Throughput driver for the MediumNested workload, zserio-c side.
 * Same shape as the small_flat driver — see that file for the
 * harness convention. */

#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "benchmarks/medium_nested/medium_nested.h"

static double ns_diff(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) * 1e9
         + (double)(b.tv_nsec - a.tv_nsec);
}

/* Fill one Inner with deterministic values keyed on `k` so each of
 * the ten Inners in the outer struct carries distinct payload. */
static void fill_inner(benchmarks_medium_nested_inner_t *v, uint32_t k)
{
    benchmarks_medium_nested_inner_init(v);
    v->a = (uint8_t)(0xA0U + k);
    v->b = (uint16_t)(0x1230U + k);
    v->c = 0x12345670U + k;
    v->d = UINT64_C(0x0123456789ABCDE0) + (uint64_t)k;
    v->e = (int8_t)(-42 - (int8_t)k);
    v->f = (int16_t)(-12000 - (int16_t)k);
    v->g = (int32_t)(-1234560 - (int32_t)k);
    v->h = INT64_C(-1234567890120) - (int64_t)k;
    v->i = ((k & 1U) != 0U);
    v->j = (uint8_t)(k & 0x0FU);
}

int main(int argc, char *argv[])
{
    const int N = (argc > 1) ? atoi(argv[1]) : 50000;
    if (N <= 0) { fprintf(stderr, "usage: %s [n_iters]\n", argv[0]); return 2; }

    benchmarks_medium_nested_medium_nested_t v;
    benchmarks_medium_nested_medium_nested_init(&v);
    fill_inner(&v.a, 0U);
    fill_inner(&v.b, 1U);
    fill_inner(&v.c, 2U);
    fill_inner(&v.d, 3U);
    fill_inner(&v.e, 4U);
    fill_inner(&v.f, 5U);
    fill_inner(&v.g, 6U);
    fill_inner(&v.h, 7U);
    fill_inner(&v.i, 8U);
    fill_inner(&v.j, 9U);

    uint8_t buf[512];
    /* Warm-up + size discovery. */
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8U);
    if (benchmarks_medium_nested_medium_nested_write(&v, &w) != ZSERIO_OK) {
        fprintf(stderr, "warm-up write failed\n"); return 1;
    }
    const size_t bytes = (zserio_bitstream_writer_position(&w) + 7U) / 8U;
    const size_t bits  = zserio_bitstream_writer_position(&w);

    /* Encode timing. */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) {
        zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8U);
        (void)benchmarks_medium_nested_medium_nested_write(&v, &w);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double encode_ns = ns_diff(t0, t1) / (double)N;

    /* Decode timing. */
    const zserio_allocator_t alloc = zserio_allocator_libc();
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) {
        zserio_bitstream_reader_t r;
        zserio_bitstream_reader_init(&r, buf, bits);
        benchmarks_medium_nested_medium_nested_t out;
        benchmarks_medium_nested_medium_nested_init(&out);
        (void)benchmarks_medium_nested_medium_nested_read(&out, &r, &alloc);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double decode_ns = ns_diff(t0, t1) / (double)N;

    fprintf(stdout, "encode_ns_per_op=%.2f decode_ns_per_op=%.2f bytes=%zu\n",
            encode_ns, decode_ns, bytes);
    return 0;
}
