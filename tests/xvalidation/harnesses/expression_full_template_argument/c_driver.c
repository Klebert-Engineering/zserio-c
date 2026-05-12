/* Template arg references a fully-qualified imported enum:
 * FullTemplateArgument<templates.expression_full_template_argument.import_color.Color>. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "templates/expression_full_template_argument/full_template_argument_holder.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_expression_full_template_argument_full_template_argument_holder_t pkg;
    templates_expression_full_template_argument_full_template_argument_holder_init(&pkg);

    pkg.template_argument_internal.bool_field = (seed & 1u) ? true : false;
    pkg.template_argument_internal.expression_field = (int32_t)(seed * 191u);
    pkg.template_argument_external.bool_field = (seed & 2u) ? true : false;
    pkg.template_argument_external.expression_field = (int32_t)(seed * 13u);

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_expression_full_template_argument_full_template_argument_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
