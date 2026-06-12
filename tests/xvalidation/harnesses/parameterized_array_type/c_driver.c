/* Optional condition reads through a nested parameterized array's
 * element: `holder.array[0].value1 == 0`. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/parameterized_array_type/parameterized_array_type_expression.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_parameterized_array_type_parameterized_array_type_expression_t pkg;
    expressions_parameterized_array_type_parameterized_array_type_expression_init(&pkg);

    /* holder is constructed with needsExtra=false → value2 never present. */
    /* For odd seeds force value1[0] = 0 so the optional fires. */
    if (seed & 1u)
    {
        pkg.holder.array[0].value1 = 0u;
        pkg.holder.array[1].value1 = (uint16_t)(seed * 7u);
        pkg.is_value1_zero = true;
    }
    else
    {
        pkg.holder.array[0].value1 = (uint16_t)((seed * 11u) | 1u);
        pkg.holder.array[1].value1 = (uint16_t)(seed * 13u);
    }

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_parameterized_array_type_parameterized_array_type_expression_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
