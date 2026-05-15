/* Verifies: REQ-PF-001
 *
 * Decode-only driver for upstream's addressbook_packed benchmark. Reads
 * the wire-format BLOB produced by bootstrap.cpp (from the upstream
 * JSON dataset) and times zserio-c's decode in a tight loop.
 *
 * Pattern matches the ndslive_tile driver: load file once, warm-up
 * decode, then N decode iterations with a noinline `decode_one`
 * wrapper to defeat LTO loop-invariant hoisting. */

#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "addressbook_packed/address_book.h"

static double ns_diff(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) * 1e9
         + (double)(b.tv_nsec - a.tv_nsec);
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
    addressbook_packed_address_book_t book;
    addressbook_packed_address_book_init(&book);
    (void)addressbook_packed_address_book_read(&book, &r, alloc);

    /* Deep sink: pack three fields into different byte positions of
     * a uint64 so accidental XOR cancellation can't fake a 0. Sum
     * the ids of the people for good measure — that requires every
     * person record to have been decoded. */
    uint64_t s = (uint64_t)book.people_count << 48;
    uint32_t id_sum = 0U;
    for (size_t i = 0U; i < book.people_count; ++i) {
        id_sum += (uint32_t)book.people[i].id;
    }
    s |= (uint64_t)id_sum;
    if (book.people_count > 0U) {
        const addressbook_packed_person_t *last_p =
            &book.people[book.people_count - 1U];
        s ^= ((uint64_t)last_p->phones_count << 32);
    }
    addressbook_packed_address_book_free(&book, alloc);
    return s;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <n_iters> <blob.bin>\n", argv[0]);
        return 2;
    }
    const int N = atoi(argv[1]);
    if (N <= 0) { fprintf(stderr, "bad n_iters\n"); return 2; }

    FILE *f = fopen(argv[2], "rb");
    if (f == NULL) { fprintf(stderr, "open %s\n", argv[2]); return 1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 1; }
    const long file_size = ftell(f);
    if (file_size <= 0) { fclose(f); return 1; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)file_size);
    if (buf == NULL) { fclose(f); return 1; }
    if (fread(buf, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(buf); fclose(f); return 1;
    }
    fclose(f);

    const size_t bytes = (size_t)file_size;
    const size_t bits = bytes * 8U;
    const zserio_allocator_t alloc = zserio_allocator_libc();

    /* Warm-up + sanity check. */
    if (decode_one(buf, bits, &alloc) == UINT64_C(0xDEADBEEF)) { /* keep DCE quiet */ }

    uint64_t sink = 0U;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; ++i) {
        sink ^= decode_one(buf, bits, &alloc);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double decode_ns = ns_diff(t0, t1) / (double)N;

    free(buf);
    fprintf(stdout, "encode_ns_per_op=0.00 decode_ns_per_op=%.2f bytes=%zu sink=%llu\n",
            decode_ns, bytes, (unsigned long long)sink);
    return 0;
}
