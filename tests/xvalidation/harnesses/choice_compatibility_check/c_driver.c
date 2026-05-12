/* ChoiceCompatibilityCheck<HolderVersion1> — `packed HolderVersion1[]`
 * tests packed compound array where elements contain a Choice. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "choice_types/choice_compatibility_check/choice_compatibility_check_version1.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    choice_types_choice_compatibility_check_choice_compatibility_check_version1_t pkg;
    choice_types_choice_compatibility_check_choice_compatibility_check_version1_init(&pkg);

    static char text[8][32];
    const size_t n_unpacked = 3u;
    const size_t n_packed = 3u;

    pkg.array_count = n_unpacked;
    pkg.array = (choice_types_choice_compatibility_check_holder_version1_t *)calloc(
            n_unpacked, sizeof(*pkg.array));
    for (size_t i = 0; i < n_unpacked; ++i) {
        const unsigned which = (seed + (unsigned)i) % 2u;
        if (which == 0u) {
            pkg.array[i].selector =
                (choice_types_choice_compatibility_check_enum_version1_t)0; /* COORD_XY */
            pkg.array[i].choice_field.u.coord_xy.coord_x = (uint32_t)(seed * 7u + (unsigned)i);
            pkg.array[i].choice_field.u.coord_xy.coord_y = (uint32_t)(seed * 11u + (unsigned)i);
        } else {
            pkg.array[i].selector =
                (choice_types_choice_compatibility_check_enum_version1_t)5; /* TEXT */
            int len = snprintf(text[i], sizeof(text[i]), "t-%u-%zu", seed, i);
            pkg.array[i].choice_field.u.text.bytes = text[i];
            pkg.array[i].choice_field.u.text.length = (size_t)len;
        }
    }

    pkg.packed_array_count = n_packed;
    pkg.packed_array = (choice_types_choice_compatibility_check_holder_version1_t *)calloc(
            n_packed, sizeof(*pkg.packed_array));
    for (size_t i = 0; i < n_packed; ++i) {
        pkg.packed_array[i].selector =
            (choice_types_choice_compatibility_check_enum_version1_t)0; /* COORD_XY (packable) */
        pkg.packed_array[i].choice_field.u.coord_xy.coord_x = (uint32_t)(seed * 13u + (unsigned)i);
        pkg.packed_array[i].choice_field.u.coord_xy.coord_y = (uint32_t)(seed * 17u + (unsigned)i);
    }

    uint8_t buf[1024];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_choice_compatibility_check_choice_compatibility_check_version1_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
