/* Optional condition references a fully-qualified package-rooted enum value. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/full_enumeration_type/full_enumeration_type_expression.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_full_enumeration_type_full_enumeration_type_expression_t pkg;
    expressions_full_enumeration_type_full_enumeration_type_expression_init(&pkg);

    /* RED keeps the optional present; BLUE keeps it absent. Alternate by seed parity. */
    pkg.color = (seed & 1u)
        ? expressions_full_enumeration_type_COLOR_RED
        : expressions_full_enumeration_type_COLOR_BLUE;
    if (pkg.color == expressions_full_enumeration_type_COLOR_RED)
        pkg.is_color_red = true;

    uint8_t buf[4];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_full_enumeration_type_full_enumeration_type_expression_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
