/* Verifies: REQ-PF-001 — decode-only driver, upstream carsales_packed. */

#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "carsales_packed/car_sales.h"

static double ns_diff(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
}

#if defined(__GNUC__) || defined(__clang__)
#  define ZSERIO_BENCH_NOINLINE __attribute__((noinline))
#else
#  define ZSERIO_BENCH_NOINLINE
#endif

static ZSERIO_BENCH_NOINLINE uint64_t decode_one(const uint8_t *buf, size_t bits,
                                                 const zserio_allocator_t *alloc)
{
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, bits);
    carsales_packed_car_sales_t v;
    carsales_packed_car_sales_init(&v);
    (void)carsales_packed_car_sales_read(&v, &r, alloc);

    /* Deep sink: count + sum of prices forces every CarSale element
     * to be decoded down to its `price` field. */
    uint64_t s = (uint64_t)v.num_sold_cars << 48;
    uint64_t price_sum = 0U;
    for (size_t i = 0U; i < v.car_sales_count; ++i) {
        price_sum += (uint64_t)v.car_sales[i].price;
    }
    s ^= price_sum;
    carsales_packed_car_sales_free(&v, alloc);
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

    const size_t bytes = (size_t)sz;
    const size_t bits = bytes * 8U;
    const zserio_allocator_t alloc = zserio_allocator_libc();
    (void)decode_one(buf, bits, &alloc);

    uint64_t sink = 0U;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) sink ^= decode_one(buf, bits, &alloc);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double decode_ns = ns_diff(t0, t1) / (double)N;

    free(buf);
    fprintf(stdout, "encode_ns_per_op=0.00 decode_ns_per_op=%.2f bytes=%zu sink=%llu\n",
            decode_ns, bytes, (unsigned long long)sink);
    return 0;
}
