/* Verifies: REQ-PF-001
 *
 * Decode-only throughput driver for the NDS Live SmartLayerTile
 * fixture (50,896-byte upstream-encoded blob — see
 * conformance/ndslive/tile.bin). This is the most realistic of our
 * workloads: a real production-shape payload that exercises packed
 * arrays of compound elements, varints, indexed offsets,
 * parametrized types, and deep compound nesting all in the same
 * decode pass.
 *
 * Encode is intentionally not measured. Constructing a SmartLayerTile
 * value from scratch isn't a realistic production operation; decoding
 * one from network or storage is. The harness still emits an
 * `encode_ns_per_op=0` field for output-format consistency. */

#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/extern.h>

#include "nds/smart/tile/smart_layer_tile.h"
#include "nds/smart/types/data_layer.h"

static double ns_diff(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) * 1e9
         + (double)(b.tv_nsec - a.tv_nsec);
}

/* Per-iteration decode in a noinline wrapper. With -O2 + LTO the
 * compiler otherwise proves each iteration produces the same result
 * (the input buffer is unchanged) and hoists the entire decode out
 * of the timed loop, giving a meaningless near-zero time. The
 * noinline attribute forces a real function call per iteration, so
 * the inputs (buf, bits, alloc) are treated as opaque each time and
 * the decode actually runs. */
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
    nds_smart_tile_smart_layer_tile_t tile;
    nds_smart_tile_smart_layer_tile_init(&tile);
    (void)nds_smart_tile_smart_layer_tile_read(&tile, &r, alloc);

    /* Deep sink so the wrapper's return value depends on actually
     * reaching the last byte of the last layer's extern payload. */
    uint64_t s = (uint64_t)tile.header.num_data_layers;
    if (tile.layers_count > 0U) {
        const nds_smart_types_data_layer_t *last =
            &tile.layers[tile.layers_count - 1U];
        const zserio_extern_t *blob = &last->layer.data;
        if ((blob->bytes != NULL) && (blob->bit_length >= 8U)) {
            const size_t last_byte = (blob->bit_length - 1U) / 8U;
            s ^= (uint64_t)blob->bytes[last_byte];
        }
    }
    nds_smart_tile_smart_layer_tile_free(&tile, alloc);
    return s;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <n_iters> <tile.bin>\n", argv[0]);
        return 2;
    }
    const int N = atoi(argv[1]);
    if (N <= 0) { fprintf(stderr, "bad n_iters\n"); return 2; }

    /* Load tile.bin once. */
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

    /* Warm-up + sanity check that decode reaches OK. */
    {
        const zserio_allocator_t alloc = zserio_allocator_libc();
        zserio_bitstream_reader_t r;
        (void)zserio_bitstream_reader_init(&r, buf, bits);
        nds_smart_tile_smart_layer_tile_t tile;
        nds_smart_tile_smart_layer_tile_init(&tile);
        const zserio_status_t st =
            nds_smart_tile_smart_layer_tile_read(&tile, &r, &alloc);
        if (st != ZSERIO_OK) {
            fprintf(stderr, "warm-up decode failed: status=%d\n", (int)st);
            nds_smart_tile_smart_layer_tile_free(&tile, &alloc);
            free(buf);
            return 1;
        }
        nds_smart_tile_smart_layer_tile_free(&tile, &alloc);
    }

    /* Decode timing. */
    const zserio_allocator_t alloc = zserio_allocator_libc();
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
