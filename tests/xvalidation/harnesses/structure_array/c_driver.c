/* Schema function indexing an array: `getElement()` returns
 * `values[pos]`. The function isn't on the wire — we just write
 * numElements + values + pos. The function must compile with our
 * codegen (verifies `self->values[self->pos]` lowering). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/structure_array/structure_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_structure_array_structure_array_t pkg;
    functions_structure_array_structure_array_init(&pkg);

    pkg.num_elements = (uint16_t)((seed % 5u) + 2u);
    pkg.values_count = pkg.num_elements;
    pkg.values = (functions_structure_array_item_t *)calloc(
            pkg.values_count, sizeof(*pkg.values));
    for (size_t i = 0; i < pkg.values_count; ++i) {
        pkg.values[i].value_a = (uint8_t)((seed + (unsigned)i) & 0xFFu);
        pkg.values[i].value_b = (uint8_t)((seed * 7u + (unsigned)i) & 0xFFu);
    }
    pkg.pos = (uint16_t)(seed % pkg.num_elements);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_structure_array_structure_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.values); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%u pos=%u bytes=%zu\n",
            seed, (unsigned)pkg.num_elements, (unsigned)pkg.pos, byte_pos);
    free(pkg.values);
    return 0;
}
