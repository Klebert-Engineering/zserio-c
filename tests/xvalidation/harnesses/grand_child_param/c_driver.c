/* Parameter propagation through a grandchild + choice + arrays:
 *   ItemChoiceHolder { bool hasItem; ItemChoice(hasItem) itemChoice; }
 *   ItemChoice on hasItem: case true → Item(true), case false → uint16
 *   GrandChildParam contains an ItemChoiceHolder, an array of them,
 *   and an optional uint32 array. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/grand_child_param/grand_child_param.h"
#include "parameterized_types/grand_child_param/item_choice_holder.h"
#include "parameterized_types/grand_child_param/item_choice.h"
#include "parameterized_types/grand_child_param/item.h"

static void fill_holder(parameterized_types_grand_child_param_item_choice_holder_t *h,
                        unsigned k, bool hasItem)
{
    parameterized_types_grand_child_param_item_choice_holder_init(h);
    h->has_item = hasItem;
    parameterized_types_grand_child_param_item_choice_set_parameters(&h->item_choice, hasItem);
    if (hasItem) {
        parameterized_types_grand_child_param_item_set_parameters(&h->item_choice.u.item, true);
        h->item_choice.u.item.param = (uint16_t)(k * 17u);
        h->item_choice.u.item.extra_param_present = true;
        h->item_choice.u.item.extra_param = (uint32_t)(k * 1103u);
    } else {
        h->item_choice.u.param = (uint16_t)(k & 0xFFFFu);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_grand_child_param_grand_child_param_t pkg;
    parameterized_types_grand_child_param_grand_child_param_init(&pkg);

    fill_holder(&pkg.item_choice_holder, seed, (seed & 1u) != 0u);

    pkg.item_choice_holder_array_count = (size_t)((seed % 3u) + 1u);
    pkg.item_choice_holder_array =
        (parameterized_types_grand_child_param_item_choice_holder_t *)calloc(
                pkg.item_choice_holder_array_count, sizeof(*pkg.item_choice_holder_array));
    for (size_t i = 0; i < pkg.item_choice_holder_array_count; ++i)
        fill_holder(&pkg.item_choice_holder_array[i], seed + (unsigned)i,
                    ((seed + (unsigned)i) & 1u) != 0u);

    /* Optional array: present iff bit 1 of seed. */
    if ((seed >> 1) & 1u) {
        pkg.dummy_array_present = true;
        pkg.dummy_array_count = 2;
        pkg.dummy_array = (uint32_t *)calloc(2, sizeof(uint32_t));
        pkg.dummy_array[0] = 0xCAFEu;
        pkg.dummy_array[1] = 0xBEEFu;
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_grand_child_param_grand_child_param_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
