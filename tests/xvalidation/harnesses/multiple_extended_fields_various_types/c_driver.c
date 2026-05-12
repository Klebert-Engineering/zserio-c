/* Extended1 from multiple_extended_fields_various_types — a moderate
 * stack of `extend` fields: int:7 + optional uint32 + extern + bytes.
 * The .zs file also defines Extended2 which contains
 * `extend optional Union(...) extendedValue8[]` exercising the
 * parametised packed-union path; that's tested via Extended1 + the
 * codegen succeeding for Union2's packed helpers. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/bytes.h>
#include <zserio/extern.h>
#include "extended_members/multiple_extended_fields_various_types/extended1.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_multiple_extended_fields_various_types_extended1_t pkg;
    extended_members_multiple_extended_fields_various_types_extended1_init(&pkg);

    pkg.value = (int8_t)(((int)(seed & 0x3Fu)) - 32); /* int:7 → [-64, 63] */
    if (seed & 1u) {
        pkg.extended_value1 = (uint32_t)(seed * 1103u);
        pkg.extended_value1_present = true;
    }

    static uint8_t ext_bytes[] = {0xAA, 0xBB, 0xCC};
    pkg.extended_value2.bytes = ext_bytes;
    pkg.extended_value2.bit_length = 19u;

    static uint8_t b_bytes[5];
    for (int i = 0; i < 5; ++i) b_bytes[i] = (uint8_t)(seed + (unsigned)i * 13u);
    pkg.extended_value3.bytes = b_bytes;
    pkg.extended_value3.length = 5u;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_multiple_extended_fields_various_types_extended1_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
