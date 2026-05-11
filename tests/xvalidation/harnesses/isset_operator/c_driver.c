/* `isset(bitmask, bits)` operator in schema functions and in
 * `if` clauses. The TestBitmask has INT, STRING, BOTH bits. The
 * `Parameterized(TestBitmask param)` struct conditions intField on
 * isset(param, INT) and stringField on isset(param, STRING). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/isset_operator/is_set_operator.h"
#include "expressions/isset_operator/parameterized.h"
#include "expressions/isset_operator/test_bitmask.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_isset_operator_is_set_operator_t pkg;
    expressions_isset_operator_is_set_operator_init(&pkg);

    /* Pick a bitmask value; we want both fields present sometimes,
     * neither, or one — exercise the optional clauses. */
    static const uint8_t kPerm[] = { 0x00u, 0x01u, 0x02u, 0x03u };  /* NONE, INT, STRING, BOTH */
    const uint8_t v = kPerm[seed % 4u];
    pkg.test_bitmask = v;
    expressions_isset_operator_parameterized_set_parameters(&pkg.parameterized, v);
    if (v & 0x01u) {
        pkg.parameterized.int_field_present = true;
        pkg.parameterized.int_field = (uint32_t)(seed * 1103u);
    }
    if (v & 0x02u) {
        const char *s = (seed & 1u) ? "isset.test" : "string.field";
        pkg.parameterized.string_field_present = true;
        pkg.parameterized.string_field.bytes = (char *)s;
        pkg.parameterized.string_field.length = strlen(s);
    }

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_isset_operator_is_set_operator_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u v=0x%x bytes=%zu\n", seed, v, byte_pos);
    return 0;
}
