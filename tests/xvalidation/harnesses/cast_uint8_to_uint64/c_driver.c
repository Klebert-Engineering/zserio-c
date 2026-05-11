/* Schema function with ternary returning either a const-1 or a uint8
 * — both narrower than the declared uint64 return type, exercising
 * implicit widening. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/cast_uint8_to_uint64/cast_u_int8_to_u_int64_expression.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_cast_uint8_to_uint64_cast_u_int8_to_u_int64_expression_t pkg;
    expressions_cast_uint8_to_uint64_cast_u_int8_to_u_int64_expression_init(&pkg);
    pkg.uint8_value = (uint8_t)(seed & 0xFFu);
    pkg.use_constant = (seed & 1u) != 0u;

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_cast_uint8_to_uint64_cast_u_int8_to_u_int64_expression_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
