/* Optional whose condition compares against a top-level const. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/const_type/const_type_expression.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_const_type_const_type_expression_t pkg;
    expressions_const_type_const_type_expression_init(&pkg);

    pkg.value = (uint8_t)((seed | 1u) & 0x7Fu); /* always != 0 → optional fires. */
    pkg.additional_value = (uint8_t)((seed >> 1) & 0x07u);

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_const_type_const_type_expression_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
