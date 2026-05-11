/* Choice selector contains a template-arg-driven shift:
 * `on selector + valueof(E.SHIFT)`. Different E gives different routing. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "templates/choice_templated_selector/choice_templated_selector.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_choice_templated_selector_choice_templated_selector_t pkg;
    templates_choice_templated_selector_choice_templated_selector_init(&pkg);

    /* Shift16.SHIFT = 0: selector16 + 0 ∈ {0, 1, 2}. */
    const unsigned cycle = seed % 3u;
    pkg.selector16 = (uint16_t)cycle;
    /* Shift32.SHIFT = 1: selector32 + 1 ∈ {0, 1, 2} → selector32 ∈ {-1, 0, 1}. */
    pkg.selector32 = (uint32_t)((cycle == 0u) ? 0xFFFFFFFFu : (cycle - 1u));

    /* The choice's `case 0/1/2` arms map to uint16_field, uint32_field,
     * string_field respectively. */
    switch (cycle) {
        case 0u:
            pkg.uint16_choice.u.uint16_field = (uint16_t)((seed * 7u) & 0xFFFFu);
            pkg.uint32_choice.u.uint16_field = (uint16_t)((seed * 11u) & 0xFFFFu);
            break;
        case 1u:
            pkg.uint16_choice.u.uint32_field = (uint32_t)(seed * 13u);
            pkg.uint32_choice.u.uint32_field = (uint32_t)(seed * 17u);
            break;
        default: {
            static char a[32], b[32];
            int na = snprintf(a, sizeof(a), "16-%u", seed);
            int nb = snprintf(b, sizeof(b), "32-%u", seed);
            pkg.uint16_choice.u.string_field.bytes = a;
            pkg.uint16_choice.u.string_field.length = (size_t)na;
            pkg.uint32_choice.u.string_field.bytes = b;
            pkg.uint32_choice.u.string_field.length = (size_t)nb;
        } break;
    }

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_choice_templated_selector_choice_templated_selector_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
