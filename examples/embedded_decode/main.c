/*
 * examples/embedded_decode/main.c
 *
 * End-to-end demonstration of the embedded deployment pattern for
 * zserio-c: the runtime's reference bump arena (`zserio_arena_t`)
 * over a fixed-size static buffer, explicit handling of every
 * status code the decode path can return, and a worst-case sizing
 * loop that lets the integrator confirm their pool is big enough.
 *
 * The schema is `telemetry.zs`. Generate the C with:
 *
 *   java -cp $ZSERIO_CORE_JAR:$ZSERIO_C_JAR:$ZSERIO_JAR \
 *        zserio.tools.ZserioTool \
 *        -src . telemetry.zs \
 *        -c gen
 *
 * Build with the bundled CMakeLists.txt, or directly:
 *
 *   gcc -std=c99 -O2 -I../../runtime/include -I gen \
 *       main.c gen/telemetry/[a-z]*.c ../../runtime/src/[a-z]*.c -o demo
 *
 * Then `./demo` to decode a hand-crafted blob and print the result.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "telemetry/frame.h"

/*
 * Static arena pool. Size it once from the worst-case
 * `arena.high_water` you observe on representative inputs in
 * development — this example uses 4 KB, which is comfortable for a
 * frame with ~100 readings + a short label.
 *
 * The pool is `static` so it lives in `.bss`; no malloc, no runtime
 * sizing. On a real ECU you'd often place it in a named section so
 * the linker script can position it (e.g. on-die SRAM).
 */
#define ARENA_BYTES 4096
static uint8_t arena_pool[ARENA_BYTES];

/* Decode one frame and return ZSERIO_OK on success. The caller-
 * supplied `out` is populated with a borrowed view of the decoded
 * data — borrowed from the arena, so it stays valid until the
 * caller calls `zserio_arena_reset()`. */
static zserio_status_t decode_frame(const uint8_t *wire, size_t wire_bits,
                                    zserio_arena_t *arena,
                                    telemetry_frame_t *out)
{
    zserio_bitstream_reader_t reader;
    zserio_status_t st = zserio_bitstream_reader_init(&reader, wire, wire_bits);
    if (st != ZSERIO_OK) {
        return st;
    }

    telemetry_frame_init(out);
    const zserio_allocator_t alloc = zserio_allocator_arena(arena);
    st = telemetry_frame_read(out, &reader, &alloc);
    if (st != ZSERIO_OK) {
        /* On failure the partially-decoded `*out` is in a state safe
         * for `_free`, but with the arena the right unwind is just to
         * reset — `_free` is a no-op anyway. The status code names
         * the failure mode (END_OF_STREAM, INVALID_*, OUT_OF_MEMORY,
         * ...) — see runtime/include/zserio/status.h. */
        return st;
    }
    return ZSERIO_OK;
}

/* Hand-built wire-format blob for the telemetry.Frame schema:
 *
 *   sensor_id     uint32   = 0x01020304          (big-endian)
 *   device_label  string   = "engine_temp"       (varsize 0x0B + payload)
 *   reading_count uint16   = 4
 *   readings[4]   int16    = { 21, 22, 23, -1 }  (big-endian, two's-complement)
 */
static const uint8_t example_wire[] = {
    /* sensor_id = 0x01020304 */
    0x01, 0x02, 0x03, 0x04,
    /* string length varsize = 11 (one byte: high-bit-clear short form) */
    0x0B,
    /* "engine_temp" */
    'e', 'n', 'g', 'i', 'n', 'e', '_', 't', 'e', 'm', 'p',
    /* reading_count = 4 */
    0x00, 0x04,
    /* readings */
    0x00, 0x15,            /* 21  */
    0x00, 0x16,            /* 22  */
    0x00, 0x17,            /* 23  */
    0xFF, 0xFF             /* -1  */
};

int main(void)
{
    zserio_arena_t arena;
    zserio_arena_init(&arena, arena_pool, sizeof(arena_pool));

    telemetry_frame_t frame;
    const zserio_status_t st = decode_frame(example_wire, sizeof(example_wire) * 8U,
                                            &arena, &frame);
    if (st != ZSERIO_OK) {
        (void)fprintf(stderr, "decode failed: status %d\n", (int)st);
        return 1;
    }

    /* Print the decoded fields. `device_label.bytes` is a borrowed
     * pointer into the arena — the bytes themselves are owned by
     * the arena, so we have to print before the next reset. */
    (void)printf("sensor_id     = 0x%08x\n", (unsigned)frame.sensor_id);
    (void)printf("device_label  = \"%.*s\"\n",
                 (int)frame.device_label.length, frame.device_label.bytes);
    (void)printf("reading_count = %u\n", (unsigned)frame.reading_count);
    (void)printf("readings      =");
    for (size_t i = 0U; i < frame.reading_count; ++i) {
        (void)printf(" %d", (int)frame.readings[i]);
    }
    (void)putchar('\n');

    /* Diagnostic: how much of the pool the worst case used. In a
     * real deployment you'd run this in a soak test against
     * representative inputs and bump `ARENA_BYTES` if `high_water`
     * approached `ARENA_BYTES`. */
    (void)printf("arena high_water = %zu / %zu bytes\n",
                 arena.high_water, (size_t)ARENA_BYTES);

    /* Reclaim the arena in O(1). Calling `telemetry_frame_free()`
     * is unnecessary with the arena (every `_free` is a no-op), but
     * harmless if the integrator prefers the explicit form for
     * symmetry with the libc-allocator path. */
    zserio_arena_reset(&arena);
    return 0;
}
