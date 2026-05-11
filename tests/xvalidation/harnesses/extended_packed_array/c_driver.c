/* `extend packed T[]` — adds a packed-array field as a backward-
 * compat extension. Schema:
 *   struct Extended2 {
 *       Element array[];
 *       extend packed Element packedArray[];
 *       extend optional packed Element optionalPackedArray[];
 *   };
 * Element { uint32 value = 13; }. The extended-optional field
 * carries a 1-bit presence prefix. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "extended_members/extended_packed_array/extended2.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_extended_packed_array_extended2_t pkg;
    extended_members_extended_packed_array_extended2_init(&pkg);

    pkg.array_count = (size_t)((seed % 3u) + 1u);
    pkg.array = (extended_members_extended_packed_array_element_t *)calloc(
            pkg.array_count, sizeof(*pkg.array));
    for (size_t i = 0; i < pkg.array_count; ++i)
        pkg.array[i].value = (uint32_t)(seed + (unsigned)i * 100u);

    pkg.packed_array_count = (size_t)((seed % 4u) + 2u);
    pkg.packed_array = (extended_members_extended_packed_array_element_t *)calloc(
            pkg.packed_array_count, sizeof(*pkg.packed_array));
    for (size_t i = 0; i < pkg.packed_array_count; ++i)
        pkg.packed_array[i].value = (uint32_t)(seed * 7u + (unsigned)i);

    if ((seed >> 1) & 1u) {
        pkg.optional_packed_array_present = true;
        pkg.optional_packed_array_count = 3;
        pkg.optional_packed_array = (extended_members_extended_packed_array_element_t *)calloc(
                3, sizeof(*pkg.optional_packed_array));
        for (size_t i = 0; i < 3; ++i)
            pkg.optional_packed_array[i].value = (uint32_t)(seed * 11u + (unsigned)i * 13u);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_extended_packed_array_extended2_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
