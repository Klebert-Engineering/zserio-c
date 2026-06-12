/* extend optional Parameterized(value) — combines extend, optional, and
 * a parameterized compound that reads its parameter from a sibling field. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "extended_members/extended_optional_parameterized_field/extended.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_extended_optional_parameterized_field_extended_t pkg;
    extended_members_extended_optional_parameterized_field_extended_init(&pkg);

    pkg.value = (uint16_t)((seed % 3u) + 2u); /* size of the inner string array */
    pkg.extended_value.array_count = (size_t)pkg.value;
    pkg.extended_value.array = (zserio_string_t *)calloc(pkg.extended_value.array_count, sizeof(zserio_string_t));
    static char text[8][32];
    for (size_t i = 0; i < pkg.extended_value.array_count && i < 8; ++i) {
        int n = snprintf(text[i], sizeof(text[i]), "s%u-%zu", seed, i);
        pkg.extended_value.array[i].bytes = text[i];
        pkg.extended_value.array[i].length = (size_t)n;
    }
    pkg.extended_value_present = true;

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_extended_optional_parameterized_field_extended_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
