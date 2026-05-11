/* Single string field — minimal structure historically tricky for Java
 * because of file-name/field-name constructor clashes. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "structure_types/one_string_structure/one_string_structure.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    structure_types_one_string_structure_one_string_structure_t pkg;
    structure_types_one_string_structure_one_string_structure_init(&pkg);

    static char text[64];
    int n = snprintf(text, sizeof(text), "one-string-%u", seed);
    pkg.one_string.bytes = text;
    pkg.one_string.length = (size_t)n;

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = structure_types_one_string_structure_one_string_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
