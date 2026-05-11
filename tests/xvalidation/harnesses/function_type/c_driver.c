/* Conditional optional gated by a schema function call:
 *   isRedColorLight if getCurrentColor() == Color.RED;
 * getCurrentColor() returns self->color; so isRedColorLight present
 * iff color == RED. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/function_type/function_type_expression.h"
#include "expressions/function_type/color.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_function_type_function_type_expression_t pkg;
    expressions_function_type_function_type_expression_init(&pkg);

    /* RED=0, BLUE=1 per schema. */
    pkg.color = (seed & 1u) ? 1u : 0u;
    pkg.is_red_color_light_present = (pkg.color == 0u);
    if (pkg.is_red_color_light_present)
        pkg.is_red_color_light = (seed & 2u) != 0u;

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_function_type_function_type_expression_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u color=%u bytes=%zu\n", seed, (unsigned)pkg.color, byte_pos);
    return 0;
}
