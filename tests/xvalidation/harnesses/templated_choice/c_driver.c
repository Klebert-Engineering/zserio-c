/* Templated choice: TestChoice<T, U, S>(S selector) on selector.field.
 * case 0 → T field, case 1 → U(selector) paramField. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/bytes.h>
#include <zserio/string.h>
#include "choice_types/templated_choice/templated_choice.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    choice_types_templated_choice_templated_choice_t pkg;
    choice_types_templated_choice_templated_choice_init(&pkg);

    /* selector.field controls the choice arm: 0 picks T, 1 picks U(selector). */
    pkg.bytes_selector.field = (uint8_t)(seed & 1u);
    pkg.struct_selector.field = (uint8_t)((seed >> 1) & 1u);

    if (pkg.bytes_selector.field == 0u) {
        static uint8_t b[5];
        for (int i = 0; i < 5; ++i) b[i] = (uint8_t)(seed + (unsigned)i);
        pkg.bytes_choice.u.field.bytes = b;
        pkg.bytes_choice.u.field.length = 5u;
    } else {
        /* case 1 → TestUnion(selector); selector.field == 1.
         * Union arms: strField, arrayField[param.field=1]. Pick strField. */
        static char text[32];
        int n = snprintf(text, sizeof(text), "b-%u", seed);
        pkg.bytes_choice.u.param_field.tag = choice_types_templated_choice_TEST_UNION_TAG_STR_FIELD;
        pkg.bytes_choice.u.param_field.u.str_field.bytes = text;
        pkg.bytes_choice.u.param_field.u.str_field.length = (size_t)n;
    }

    if (pkg.struct_selector.field == 0u) {
        pkg.struct_choice.u.field.varuint_field = (int64_t)(seed * 13u);
        pkg.struct_choice.u.field.float64_field = (double)(seed * 0.5);
    } else {
        static char text[32];
        int n = snprintf(text, sizeof(text), "s-%u", seed);
        pkg.struct_choice.u.param_field.tag = choice_types_templated_choice_TEST_UNION_TAG_STR_FIELD;
        pkg.struct_choice.u.param_field.u.str_field.bytes = text;
        pkg.struct_choice.u.param_field.u.str_field.length = (size_t)n;
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_templated_choice_templated_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
