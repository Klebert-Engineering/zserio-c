#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/enumeration_type/enumeration_type_expression.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_enumeration_type_enumeration_type_expression_t pkg;
    expressions_enumeration_type_enumeration_type_expression_init(&pkg);
    /* Color values in schema: RED=0, BLUE=1. We pick by seed parity. */
    pkg.color = (expressions_enumeration_type_color_t)(seed & 1u);
    pkg.is_color_red_present = (pkg.color == 0u);
    pkg.is_color_red = pkg.is_color_red_present ? (seed & 2u) != 0u : false;

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_enumeration_type_enumeration_type_expression_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u color=%u bytes=%zu\n", seed, (unsigned)pkg.color, byte_pos);
    return 0;
}
