/* Template parameter shadows a real enum named `E`: the optional
 * condition mentions BOTH `E.E` (template arg's item, Letters.E)
 * AND fully-qualified `templates.expression_enum_template_argument_conflict.E.BLACK`. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "templates/expression_enum_template_argument_conflict/enum_template_argument_conflict_holder.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_expression_enum_template_argument_conflict_enum_template_argument_conflict_holder_t pkg;
    templates_expression_enum_template_argument_conflict_enum_template_argument_conflict_holder_init(&pkg);

    pkg.enum_template_argument_conflict.bool_field = (seed & 1u) ? true : false;
    pkg.enum_template_argument_conflict.expression_field = (int32_t)(seed * 191u);

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_expression_enum_template_argument_conflict_enum_template_argument_conflict_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
