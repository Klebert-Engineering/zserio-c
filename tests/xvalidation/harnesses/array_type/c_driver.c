/* Optional field whose condition reads from an int8 fixed array. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/array_type/array_type_expression.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_array_type_array_type_expression_t pkg;
    expressions_array_type_array_type_expression_init(&pkg);

    /* For odd seeds force both array elements to zero so the optional fires. */
    if (seed & 1u) {
        pkg.array[0] = 0;
        pkg.array[1] = 0;
    } else {
        pkg.array[0] = (int8_t)(seed % 100u);
        pkg.array[1] = (int8_t)(-(int)(seed % 100u));
    }
    pkg.is_zeros_array_valid = true;

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_array_type_array_type_expression_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
