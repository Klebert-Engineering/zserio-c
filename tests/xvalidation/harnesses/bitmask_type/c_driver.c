#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/bitmask_type/bitmask_type_expression.h"
#include "expressions/bitmask_type/colors.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_bitmask_type_bitmask_type_expression_t pkg;
    expressions_bitmask_type_bitmask_type_expression_init(&pkg);
    /* Drive `colors` from the bottom 3 bits of seed*. */
    expressions_bitmask_type_colors_t c =
        (expressions_bitmask_type_colors_t)((seed * 3u) & 0x7u);
    pkg.colors = c;
    pkg.has_color_red_present     = (c & expressions_bitmask_type_COLORS_RED)   == expressions_bitmask_type_COLORS_RED;
    pkg.has_color_red             = pkg.has_color_red_present     ? ((seed >> 0) & 1u) != 0u : false;
    pkg.has_color_green_present   = (c & expressions_bitmask_type_COLORS_GREEN) == expressions_bitmask_type_COLORS_GREEN;
    pkg.has_color_green           = pkg.has_color_green_present   ? ((seed >> 1) & 1u) != 0u : false;
    pkg.has_color_blue_present    = (c & expressions_bitmask_type_COLORS_BLUE)  == expressions_bitmask_type_COLORS_BLUE;
    pkg.has_color_blue            = pkg.has_color_blue_present    ? ((seed >> 2) & 1u) != 0u : false;
    pkg.has_all_colors_present    = c == (expressions_bitmask_type_COLORS_RED
                                          | expressions_bitmask_type_COLORS_GREEN
                                          | expressions_bitmask_type_COLORS_BLUE);
    pkg.has_all_colors            = pkg.has_all_colors_present    ? ((seed >> 3) & 1u) != 0u : false;
    pkg.has_not_color_red_present = (c & expressions_bitmask_type_COLORS_RED)   != expressions_bitmask_type_COLORS_RED;
    pkg.has_not_color_red         = pkg.has_not_color_red_present ? ((seed >> 4) & 1u) != 0u : false;
    pkg.has_other_color_than_red_present = ((c & (uint8_t)~expressions_bitmask_type_COLORS_RED) & 0x7u) != 0u;
    pkg.has_other_color_than_red         = pkg.has_other_color_than_red_present ? ((seed >> 5) & 1u) != 0u : false;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_bitmask_type_bitmask_type_expression_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u colors=0x%x bytes=%zu\n", seed, (unsigned)c, byte_pos);
    return 0;
}
