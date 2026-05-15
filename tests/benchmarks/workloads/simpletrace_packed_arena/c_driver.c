/* Verifies: REQ-PF-001
 *
 * Decode-only driver for the simpletrace_packed schema, backed by
 * the runtime's reference bump arena allocator. Pairs with the
 * pmr cpp_driver to give the fair arena-vs-arena comparison on
 * a packed-delta-dominated payload (~5K trace Points × 2 varints
 * each, mostly small deltas). See
 * docs/benchmarks/methodology.md § Allocator choice for the
 * framing. */

#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "simpletrace_packed/simple_trace.h"

static double ns_diff(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
}

#if defined(__GNUC__) || defined(__clang__)
#  define ZSERIO_BENCH_NOINLINE __attribute__((noinline))
#else
#  define ZSERIO_BENCH_NOINLINE
#endif

/* 1 MB is comfortably above the observed high-water on the upstream
 * prague-groebenzell.json dataset (~5 K Points × ~24 B each plus the
 * trace array header). */
#define ARENA_CAPACITY (1U * 1024U * 1024U)

static ZSERIO_BENCH_NOINLINE uint64_t decode_one(const uint8_t *buf, size_t bits,
                                                 const zserio_allocator_t *alloc,
                                                 zserio_arena_t *arena)
{
    zserio_arena_reset(arena);
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, bits);
    simpletrace_packed_simple_trace_t t;
    simpletrace_packed_simple_trace_init(&t);
    (void)simpletrace_packed_simple_trace_read(&t, &r, alloc);

    /* Same deep sink as the libc variant. */
    uint64_t s = (uint64_t)t.trace_count << 32;
    int64_t lon_xor = 0;
    for (size_t i = 0U; i < t.trace_count; ++i) {
        lon_xor ^= t.trace[i].longitude;
    }
    s ^= (uint64_t)lon_xor;
    /* No _free needed — arena reset reclaims everything next iteration. */
    return s;
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <n> <blob>\n", argv[0]); return 2; }
    const int N = atoi(argv[1]);
    if (N <= 0) return 2;

    FILE *f = fopen(argv[2], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END); const long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); return 1; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return 1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 1; }
    fclose(f);

    uint8_t *arena_buf = (uint8_t *)malloc(ARENA_CAPACITY);
    if (arena_buf == NULL) { free(buf); return 1; }
    zserio_arena_t arena;
    zserio_arena_init(&arena, arena_buf, ARENA_CAPACITY);
    const zserio_allocator_t alloc = zserio_allocator_arena(&arena);

    const size_t bytes = (size_t)sz;
    const size_t bits = bytes * 8U;
    (void)decode_one(buf, bits, &alloc, &arena);

    uint64_t sink = 0U;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) sink ^= decode_one(buf, bits, &alloc, &arena);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double decode_ns = ns_diff(t0, t1) / (double)N;

    free(arena_buf);
    free(buf);
    fprintf(stdout, "encode_ns_per_op=0.00 decode_ns_per_op=%.2f bytes=%zu sink=%llu arena_high_water=%zu\n",
            decode_ns, bytes, (unsigned long long)sink, arena.high_water);
    return 0;
}
