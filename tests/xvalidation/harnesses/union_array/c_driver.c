/* Union with parameter referring to a sibling struct's array, plus
 * three schema functions on the union. Wire: OuterArray bytes +
 * union tag + (uint16 position OR Item item). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/union_array/inner.h"
#include "functions/union_array/item_ref.h"
#include "functions/union_array/outer_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_union_array_inner_t pkg;
    functions_union_array_inner_init(&pkg);

    pkg.outer_array.num_elements = (uint16_t)((seed % 3u) + 2u);
    pkg.outer_array.values_count = pkg.outer_array.num_elements;
    pkg.outer_array.values = (functions_union_array_item_t *)calloc(
            pkg.outer_array.values_count, sizeof(*pkg.outer_array.values));
    for (uint16_t i = 0; i < pkg.outer_array.num_elements; ++i) {
        pkg.outer_array.values[i].value_a = (uint8_t)((seed + i) & 0xFFu);
        pkg.outer_array.values[i].value_b = (uint8_t)((seed * 7u + i) & 0xFFu);
    }

    /* ItemRef union: pick arm by parity. */
    pkg.ref.array = pkg.outer_array;
    pkg.ref.tag = (seed & 1u) ? 0u : 1u;
    if (pkg.ref.tag == 0u) {
        pkg.ref.u.item.value_a = (uint8_t)(seed & 0xFFu);
        pkg.ref.u.item.value_b = (uint8_t)((seed * 11u) & 0xFFu);
    } else {
        pkg.ref.u.position = (uint16_t)(seed % pkg.outer_array.num_elements);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_union_array_inner_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
