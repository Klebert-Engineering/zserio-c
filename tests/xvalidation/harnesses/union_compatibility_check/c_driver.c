/* Templated UnionCompatibilityCheck<UNION> instantiated as Version1,
 * holding both an unpacked and a packed array of UnionVersion1.
 * UnionVersion1 has CoordXY and string arms — no packable scalars at
 * the arm-root level, but CoordXY contains packable uint32 subfields. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "union_types/union_compatibility_check/union_compatibility_check_version1.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    union_types_union_compatibility_check_union_compatibility_check_version1_t pkg;
    union_types_union_compatibility_check_union_compatibility_check_version1_init(&pkg);

    static char text[8][32];

    pkg.array_count = 3u;
    pkg.array = (union_types_union_compatibility_check_union_version1_t *)calloc(
            pkg.array_count, sizeof(*pkg.array));
    pkg.packed_array_count = 3u;
    pkg.packed_array = (union_types_union_compatibility_check_union_version1_t *)calloc(
            pkg.packed_array_count, sizeof(*pkg.packed_array));

    /* Unpacked array: mix CoordXY and string arms. */
    for (size_t i = 0; i < pkg.array_count; ++i) {
        if (((seed + (unsigned)i) & 1u) == 0u) {
            pkg.array[i].tag = union_types_union_compatibility_check_UNION_VERSION1_TAG_COORD_XY;
            pkg.array[i].u.coord_xy.coord_x = (uint32_t)(seed * 7u + (unsigned)i);
            pkg.array[i].u.coord_xy.coord_y = (uint32_t)(seed * 11u + (unsigned)i);
        } else {
            pkg.array[i].tag = union_types_union_compatibility_check_UNION_VERSION1_TAG_TEXT;
            int n = snprintf(text[i], sizeof(text[i]), "t-%u-%zu", seed, i);
            pkg.array[i].u.text.bytes = text[i];
            pkg.array[i].u.text.length = (size_t)n;
        }
    }
    /* Packed array: stick to a single arm so delta-encoding can actually
     * compress (mixing arms across elements means each element re-reads
     * the tag and resets the delta context). */
    for (size_t i = 0; i < pkg.packed_array_count; ++i) {
        pkg.packed_array[i].tag = union_types_union_compatibility_check_UNION_VERSION1_TAG_COORD_XY;
        pkg.packed_array[i].u.coord_xy.coord_x = (uint32_t)(seed * 13u + (unsigned)i);
        pkg.packed_array[i].u.coord_xy.coord_y = (uint32_t)(seed * 17u + (unsigned)i);
    }

    uint8_t buf[512];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = union_types_union_compatibility_check_union_compatibility_check_version1_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
