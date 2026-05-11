/* Schema defaults compute lots of `<<` expressions at parse time.
 * The driver fills the runtime fields with simple values; the wire
 * format depends only on the values, not on the schema-default
 * expressions. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/left_shift_operator/left_shift_operator.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_left_shift_operator_left_shift_operator_t pkg;
    expressions_left_shift_operator_left_shift_operator_init(&pkg);

    /* Field defaults from the schema (precomputed at compile time
     * here to dodge `-Werror=shift-negative-value` on literal
     * negative LHS shifts). */
    pkg.u32 = 40u;                            /* 5 << 3 */
    pkg.i32 = -40;                            /* -5 << 3 */
    pkg.u32_complex = 32u;                    /* 1 << 2 << 3 */
    pkg.i32_complex = -32;                    /* -1 << 2 << 3 */
    pkg.u32_plus = 24u;                       /* (1+2) << 3 */
    pkg.i32_minus = -64;                      /* (4-6) << 5 */
    pkg.u32_plus_rhs_expr = 12u;              /* (1+2) << (5-3) */
    pkg.i32_minus_rhs_expr = -24;             /* (5-8) << (1+2) */
    pkg.u63_complex = ((uint64_t)11u << 9) << 11;  /* matches default */
    pkg.i64_complex = -9 * ((int64_t)1 << 9) * ((int64_t)1 << 1);

    pkg.u63 = 13u + (uint64_t)(seed & 0x3Fu);
    pkg.i64 = -13 + (int64_t)(seed & 0xFFu);
    pkg.positive_i32 = (int32_t)(13 + (int)(seed & 0xFFu));
    pkg.shift = (uint8_t)(seed % 5u + 1u);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_left_shift_operator_left_shift_operator_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
