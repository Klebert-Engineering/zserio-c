/* Kitchen-sink fixed-length array of every primitive zserio supports.
 * Schema fixes count = 5. Variable-width bitfield widths derive from
 * uint8Value, length64, length32 and schema functions: pick values
 * such that all derived widths are in [1, 64]:
 *   uint8Value = 30  → bit<uint8> width 30
 *                    getNumBitsForInt   = 30/15+1 = 3
 *                    getNumBitsForShort = 30/15-1 = 1
 *                    getNumBitsForByte  = numbits(30) = 5
 *   length64   = 32  → bit<length64> width 32
 *   length32   = 8   → int<length32> width 8 (range -128..127)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/arrays_mapping/arrays_mapping.h"

static const char *kNames[5] = { "alpha", "beta", "gamma", "delta", "epsilon" };
static const uint8_t kBytesA[] = { 0x01, 0x02, 0x03 };
static const uint8_t kBytesB[] = { 0x10 };

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_arrays_mapping_arrays_mapping_t pkg;
    array_types_arrays_mapping_arrays_mapping_init(&pkg);

    pkg.uint8_value = 30u;
    pkg.length64    = 32ull;
    pkg.length32    = 8u;

    for (size_t i = 0; i < 5; ++i) {
        const unsigned k = seed + (unsigned)i;
        pkg.uint8_array [i] = (uint8_t)(k & 0xFFu);
        pkg.uint16_array[i] = (uint16_t)(k * 17u);
        pkg.uint32_array[i] = (uint32_t)(k * 1103u);
        pkg.uint64_array[i] = (uint64_t)(k * 2654435761ull) << 16;
        pkg.int8_array  [i] = (int8_t)((int)(k & 0xFFu) - 128);
        pkg.int16_array [i] = (int16_t)((int)(k * 7u) - 1024);
        pkg.int32_array [i] = (int32_t)(k * 1103u) - 65536;
        pkg.int64_array [i] = (int64_t)(k * 2654435761ull) - (1ll << 60);

        /* fixed bitfields, value must fit width */
        pkg.bitfield8_array [i] = (uint8_t)(k & 0xFFu);
        pkg.bitfield16_array[i] = (uint16_t)(k & 0xFFFFu);
        pkg.bitfield32_array[i] = (uint32_t)(k * 1103u);
        pkg.bitfield63_array[i] = (uint64_t)(k * 2654435761ull) & ((1ull << 63) - 1);

        /* bit<uint8Value> = bit:30: range 0..2^30-1 */
        pkg.variable_bitfield_long_array [i] = (uint64_t)(k * 1103u) & ((1ull << 30) - 1);
        /* bit<getNumBitsForInt()> = bit:3: 0..7 */
        pkg.variable_bitfield_int_array  [i] = (uint64_t)(k & 0x07u);
        /* bit<getNumBitsForShort()> = bit:1: 0..1 */
        pkg.variable_bitfield_short_array[i] = (uint64_t)(k & 0x01u);
        /* bit<getNumBitsForByte()> = bit:5: 0..31 */
        pkg.variable_bitfield_byte_array [i] = (uint64_t)(k & 0x1Fu);
        /* bit<length64> = bit:32: 0..2^32-1 */
        pkg.variable_bitfield64_array    [i] = (uint64_t)(k * 1103u);

        pkg.intfield8_array [i] = (int8_t)((int)(k & 0x7Fu) - 64);
        pkg.intfield16_array[i] = (int16_t)((int)(k * 7u) - 4096);
        pkg.intfield32_array[i] = (int32_t)(k * 1103u) - 100000;
        pkg.intfield64_array[i] = (int64_t)(k * 1103u) - 100000;

        /* int<uint8Value> = int:30: range -2^29..2^29-1 */
        pkg.variable_intfield_long_array [i] = (int64_t)((int)(k & 0xFFFFu)) - 32768;
        /* int<3>: -4..3 */
        pkg.variable_intfield_int_array  [i] = (int64_t)((int)(k & 0x07u) - 4);
        /* int<1>: -1..0 */
        pkg.variable_intfield_short_array[i] = (int64_t)(-((int)(k & 0x01u)));
        /* int<5>: -16..15 */
        pkg.variable_intfield_byte_array [i] = (int64_t)((int)(k & 0x1Fu) - 16);
        /* int<length32> = int:8: -128..127 */
        pkg.variable_intfield64_array    [i] = (int64_t)((int)(k & 0xFFu) - 128);

        pkg.float16_array[i] = (float)(int)k * 0.125f;
        pkg.float32_array[i] = (float)k + 0.5f;
        pkg.float64_array[i] = (double)k * 1.5;

        pkg.varuint16_array[i] = (uint16_t)(k % 0x7FFFu);
        pkg.varuint32_array[i] = k * 100u;
        pkg.varuint64_array[i] = (uint64_t)k * 1000000ull;
        pkg.varuint_array  [i] = (uint64_t)k * 1000u;
        pkg.varsize_array  [i] = k + 1u;

        pkg.varint16_array[i] = (int16_t)((int)k - 16000);
        pkg.varint32_array[i] = (int32_t)k - 100000;
        pkg.varint64_array[i] = (int64_t)k - 1000000;
        pkg.varint_array  [i] = (int64_t)k - 50000;

        pkg.bool_array[i] = (k & 1u) != 0u;

        pkg.string_array[i].bytes  = (char *)kNames[i];
        pkg.string_array[i].length = strlen(kNames[i]);

        pkg.extern_array[i].bytes      = (uint8_t *)((i & 1u) ? kBytesB : kBytesA);
        pkg.extern_array[i].bit_length = (i & 1u) ? 7u : 19u;

        pkg.bytes_array[i].bytes  = (uint8_t *)((i & 1u) ? kBytesB : kBytesA);
        pkg.bytes_array[i].length = (i & 1u) ? sizeof(kBytesB) : sizeof(kBytesA);

        pkg.compound_array[i].id = (uint32_t)(seed * 1103u + (unsigned)i);
        pkg.compound_array[i].name.bytes = (char *)kNames[i];
        pkg.compound_array[i].name.length = strlen(kNames[i]);

        pkg.enum_array   [i] = (uint8_t)(k % 3u);
        pkg.bitmask_array[i] = (uint16_t)(((k * 7u) & 0x1u) | (((k * 7u) >> 1 & 0x1u) << 7) | (((k * 7u) >> 2 & 0x1u) << 8));
    }

    uint8_t buf[2048];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_arrays_mapping_arrays_mapping_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
