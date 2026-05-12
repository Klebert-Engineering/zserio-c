/* Three templated compounds each with field-level constraint
 * expressions; instantiated for ConstraintsInTemplates as the root.
 *
 * Notable: TemplatedUnion has `S sField[] : lengthof(sField) != 0`
 * which exercises the `lengthof` lowering for a union-arm array's
 * `_count` companion living outside the anonymous union member. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "constraints/constraints_in_templates/constraints_in_templates.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    constraints_constraints_in_templates_constraints_in_templates_t pkg;
    constraints_constraints_in_templates_constraints_in_templates_init(&pkg);

    /* structField: TemplatedStruct<TStruct, string, TestEnum> */
    pkg.struct_field.t_field.child = (int8_t)(1 + (seed % 100u));  /* != 0 */
    static char s_text[32];
    int n = snprintf(s_text, sizeof(s_text), "s-%u", seed);
    pkg.struct_field.s_field.bytes = s_text;
    pkg.struct_field.s_field.length = (size_t)n;
    pkg.struct_field.e_field =
        (constraints_constraints_in_templates_test_enum_t)0;  /* ONE → valueof <= valueof(ONE) */

    /* choiceField: TemplatedChoice<TStruct, string, TestBitmask>(2).
     * param=2 → default arm (B bField : valueof(bField) != 0). */
    pkg.choice_field.u.b_field = 1u; /* TWO (=1) != 0 */

    /* unionField: TemplatedUnion<varint64, extern, TestBitmask>.
     * Pick t_field arm: tField (varint64) != 0. */
    pkg.union_field.tag =
        constraints_constraints_in_templates_TEMPLATED_UNION_VARINT64_EXTERN_TEST_BITMASK_TAG_T_FIELD;
    pkg.union_field.u.t_field = (int64_t)((int64_t)seed * 191 + 1);

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = constraints_constraints_in_templates_constraints_in_templates_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
