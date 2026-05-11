/* Kitchen-sink schema: every primitive zserio supports, in one struct.
 * Exercises bool, fixed-width ints (8/16/32/64), unsigned & signed
 * bitfields (7..63), dynamic-bit `bit<expr>` / `int<expr>`, floats
 * (16/32/64), varuint family (16/32/64/varuint/varsize), varint family
 * (16/32/64/varint), string, extern, bytes. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "builtin_types/all_builtin_types/all_built_in_types.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);

    builtin_types_all_builtin_types_all_built_in_types_t pkg;
    builtin_types_all_builtin_types_all_built_in_types_init(&pkg);

    pkg.bool_type = true;  /* enables variable_intfield_type optional */

    /* Fixed-width ints. */
    pkg.uint8_type  = 16u;  /* governs `bit<uint8Type>` width */
    pkg.uint16_type = (uint16_t)(0x1234u ^ (seed & 0xFFu));
    pkg.uint32_type = 0x89ABCDEFu ^ (seed * 1103u);
    pkg.uint64_type = 0x0123456789ABCDEFull ^ ((uint64_t)seed << 32);
    pkg.int8_type   = (int8_t)(-1 - (int)(seed & 0x3fu));
    pkg.int16_type  = (int16_t)(-1000 + (int)(seed & 0xFFu));
    pkg.int32_type  = -100000 + (int)seed;
    pkg.int64_type  = -1000000000ll + (int64_t)seed;

    /* Unsigned bitfields. Values must fit in the declared width. */
    pkg.bitfield7_type  = 10u;  /* governs variable_intfield width */
    pkg.bitfield8_type  = 0xA5u ^ (uint8_t)seed;
    pkg.bitfield15_type = (uint16_t)((0x1234u ^ seed) & 0x7FFFu);
    pkg.bitfield16_type = (uint16_t)(0xBEEFu ^ (uint16_t)seed);
    pkg.bitfield31_type = (uint32_t)((0x12345678u ^ seed) & 0x7FFFFFFFu);
    pkg.bitfield32_type = 0xDEADBEEFu ^ seed;
    pkg.bitfield63_type = (uint64_t)(0x0FFEDCBA98765432ull ^ seed);

    pkg.variable_bitfield_type  = (uint64_t)(0xABCDu ^ seed);  /* bit:16 since uint8Type==16 */
    pkg.variable_bitfield8_type = (uint64_t)(0x5Au ^ (uint8_t)seed);  /* bit:8 (NUM_BITS=8) */

    /* Signed bitfields (fixed). */
    pkg.intfield8_type  = (int8_t)(-50 + (int)(seed & 0x3Fu));
    pkg.intfield16_type = (int16_t)(-1234 + (int)seed);
    pkg.intfield32_type = -123456 + (int)seed;
    pkg.intfield64_type = -1234567890ll + (int64_t)seed;

    /* Dynamic-width signed: bitfield7_type=10 → width=(10+1)/16+6=6 → range -32..31. */
    pkg.variable_intfield_type_present = true;
    pkg.variable_intfield_type = (int64_t)(-16 + (int)(seed & 0x1Fu));

    /* int<NUM_BITS>: width=8 → range -128..127. */
    pkg.variable_intfield8_type = (int64_t)(-32 + (int)(seed & 0x3Fu));

    /* Floats. */
    pkg.float16_type = 1.5f;
    pkg.float32_type = 1.234f + (float)(seed % 7u);
    pkg.float64_type = 3.14159265358979 + (double)(seed % 11u);

    /* varuint family. */
    pkg.varuint16_type = (uint16_t)(seed % 0xFFu);
    pkg.varuint32_type = seed * 100u;
    pkg.varuint64_type = (uint64_t)seed * 100000000ull;
    pkg.varuint_type   = (uint64_t)seed * 1000000ull;
    pkg.varsize_type   = seed * 17u + 1u;

    /* varint family. */
    pkg.varint16_type = (int16_t)(-1000 + (int)(seed % 1999u));
    pkg.varint32_type = -100000 + (int)seed;
    pkg.varint64_type = -1000000ll + (int64_t)seed;
    pkg.varint_type   = -50000 + (int64_t)seed;

    /* string / extern / bytes. */
    const char *s = (seed & 1u) ? "hello world" : "kitchen sink";
    pkg.string_type.bytes  = (char *)s;
    pkg.string_type.length = strlen(s);

    /* extern: bit_offset 0, some bytes, a few full bits. */
    static const uint8_t kExternBytes[] = { 0xAB, 0xCD, 0xEF };
    pkg.extern_type.bytes      = (uint8_t *)kExternBytes;
    pkg.extern_type.bit_length = 21u;  /* 21 bits = ~3 bytes */

    /* bytes: a fixed pattern. */
    static const uint8_t kBytes[] = { 0x10, 0x20, 0x30, 0x40 };
    pkg.bytes_type.bytes  = (uint8_t *)kBytes;
    pkg.bytes_type.length = sizeof(kBytes);

    uint8_t buf[512];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = builtin_types_all_builtin_types_all_built_in_types_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
