/* `@index` in a per-element parameter expression:
 *   Element(@index % 2 + 1 == 2) elements[length];
 * For element i, isEven = ((i%2+1) == 2) — true for odd-indexed i.
 * isEven=true → uint8 field8 arm; false → int16 field16 arm. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/index_operator/element_list.h"
#include "expressions/index_operator/element.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_index_operator_element_list_t pkg;
    expressions_index_operator_element_list_init(&pkg);

    pkg.length = (uint16_t)((seed % 5u) + 2u);
    pkg.elements_count = pkg.length;
    pkg.elements = (expressions_index_operator_element_t *)calloc(
            pkg.length, sizeof(*pkg.elements));
    for (uint16_t i = 0; i < pkg.length; ++i) {
        expressions_index_operator_element_init(&pkg.elements[i]);
        const bool isEven = ((i % 2u + 1u) == 2u);  /* matches schema */
        expressions_index_operator_element_set_parameters(&pkg.elements[i], isEven);
        if (isEven) pkg.elements[i].u.field8 = (uint8_t)((seed + i) & 0xFFu);
        else        pkg.elements[i].u.field16 = (int16_t)((int)(seed + i) - 512);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_index_operator_element_list_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u len=%u bytes=%zu\n", seed, (unsigned)pkg.length, byte_pos);
    return 0;
}
