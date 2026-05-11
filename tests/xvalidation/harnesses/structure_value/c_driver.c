/* Schema-defined function `getValue()` used as an array sizing
 * expression: `int32 items[numItems.getValue()];`. The function
 * call resolves the discriminated value (val1 / val2 / val3) at
 * write time. We pick a non-sentinel val1 so getValue() returns val1
 * directly; the array length is val1. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/structure_value/custom_var_list.h"
#include "functions/structure_value/custom_var_int.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_structure_value_custom_var_list_t pkg;
    functions_structure_value_custom_var_list_init(&pkg);

    /* Pick val1 in 1..10 (non-sentinel, so getValue() returns val1). */
    pkg.num_items.val1 = (uint8_t)((seed % 10u) + 1u);

    pkg.items_count = pkg.num_items.val1;
    pkg.items = (int32_t *)calloc(pkg.items_count, sizeof(int32_t));
    if (pkg.items == NULL) { fprintf(stderr, "alloc\n"); return 1; }
    for (size_t i = 0; i < pkg.items_count; ++i) {
        pkg.items[i] = (int32_t)((seed * 1103u + (unsigned)i * 2654435761u) ^ 0xDEADBEEFu);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_structure_value_custom_var_list_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.items); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%zu bytes=%zu\n", seed, pkg.items_count, byte_pos);
    free(pkg.items);
    return 0;
}
