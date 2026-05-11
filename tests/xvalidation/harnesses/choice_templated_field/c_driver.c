/* Choice with template type parameters used as case-arm types. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "templates/choice_templated_field/choice_templated_field.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_choice_templated_field_choice_templated_field_t pkg;
    templates_choice_templated_field_choice_templated_field_init(&pkg);

    /* selector controls which case arm both choices take. */
    pkg.selector = seed % 3u;
    if (pkg.selector == 0u) {
        pkg.choice1.u.templated_field1 = (uint32_t)(seed * 7u);
        pkg.choice2.u.templated_field1.value = (uint32_t)(seed * 11u);
    } else if (pkg.selector == 1u) {
        pkg.choice1.u.templated_field2 = (uint16_t)((seed * 5u) & 0xFFFFu);
        pkg.choice2.u.templated_field2 = (uint16_t)((seed * 13u) & 0xFFFFu);
    } else {
        static char text1[32]; int n1 = snprintf(text1, sizeof(text1), "c1-%u", seed);
        static char text2[32]; int n2 = snprintf(text2, sizeof(text2), "c2-%u", seed);
        pkg.choice1.u.string_field.bytes = text1; pkg.choice1.u.string_field.length = (size_t)n1;
        pkg.choice2.u.string_field.bytes = text2; pkg.choice2.u.string_field.length = (size_t)n2;
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_choice_templated_field_choice_templated_field_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
