/* Template arg is the choice's selector enum type, used in `case T.ONE:`. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "templates/choice_templated_enum_selector/choice_templated_enum_selector.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_choice_templated_enum_selector_choice_templated_enum_selector_t pkg;
    templates_choice_templated_enum_selector_choice_templated_enum_selector_init(&pkg);

    /* fromZero: 0/1/2 → ONE/TWO/THREE. */
    pkg.selector_from_zero =
        (templates_choice_templated_enum_selector_enum_from_zero_t)(seed % 3u);
    /* fromOne: 1/2/3 → ONE/TWO/THREE. */
    pkg.selector_from_one =
        (templates_choice_templated_enum_selector_enum_from_one_t)((seed % 3u) + 1u);

    switch ((int)pkg.selector_from_zero) {
        case 0: pkg.from_zero_choice.u.uint16_field = (uint16_t)((seed * 7u) & 0xFFFFu); break;
        case 1: pkg.from_zero_choice.u.uint32_field = (uint32_t)(seed * 11u); break;
        default: {
            static char text[32]; int n = snprintf(text, sizeof(text), "z-%u", seed);
            pkg.from_zero_choice.u.string_field.bytes = text;
            pkg.from_zero_choice.u.string_field.length = (size_t)n;
        } break;
    }
    switch ((int)pkg.selector_from_one) {
        case 1: pkg.from_one_choice.u.uint16_field = (uint16_t)((seed * 13u) & 0xFFFFu); break;
        case 2: pkg.from_one_choice.u.uint32_field = (uint32_t)(seed * 17u); break;
        default: {
            static char text[32]; int n = snprintf(text, sizeof(text), "o-%u", seed);
            pkg.from_one_choice.u.string_field.bytes = text;
            pkg.from_one_choice.u.string_field.length = (size_t)n;
        } break;
    }

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_choice_templated_enum_selector_choice_templated_enum_selector_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
