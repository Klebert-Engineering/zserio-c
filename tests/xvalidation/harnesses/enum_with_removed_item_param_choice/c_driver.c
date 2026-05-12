/* Root choice with enum selector that includes a `@removed GREY` item.
 * The schema has a case label for the removed item; we test only the
 * non-removed arms (BLACK, WHITE) plus the default. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/enum_with_removed_item_param_choice/enum_with_removed_item_param_choice.h"
#include "choice_types/enum_with_removed_item_param_choice/selector.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    /* BLACK = 0xfff, WHITE = 0x000. Both fall into the default arm
     * (only @removed GREY = 0xaaa is explicit, and we don't emit it). */
    static const uint16_t legal_vals[] = {0x0000u, 0x0fffu};
    const choice_types_enum_with_removed_item_param_choice_selector_t sel =
        (choice_types_enum_with_removed_item_param_choice_selector_t)
            legal_vals[seed % 2u];

    choice_types_enum_with_removed_item_param_choice_enum_with_removed_item_param_choice_t pkg;
    choice_types_enum_with_removed_item_param_choice_enum_with_removed_item_param_choice_init(&pkg);
    choice_types_enum_with_removed_item_param_choice_enum_with_removed_item_param_choice_set_parameters(&pkg, sel);
    /* The default arm holds `uint8 data`. */
    pkg.u.data = (uint8_t)((seed * 7u) & 0xFFu);

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_enum_with_removed_item_param_choice_enum_with_removed_item_param_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=0x%x bytes=%zu\n", seed, (unsigned)sel, byte_pos);
    return 0;
}
