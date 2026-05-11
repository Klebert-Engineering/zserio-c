/* Variable-length array of subtype-wrapped struct (subtype TestStructure
 * ArrayElement;). The wire format is unchanged by the subtype layer —
 * the array writes/reads as `TestStructure array[]`. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/variable_array_subtyped_struct/variable_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_variable_array_subtyped_struct_variable_array_t pkg;
    array_types_variable_array_subtyped_struct_variable_array_init(&pkg);

    pkg.num_elements = (uint8_t)((seed % 4u) + 1u);
    pkg.compound_array_count = pkg.num_elements;
    pkg.compound_array = (array_types_variable_array_subtyped_struct_array_element_t *)calloc(
            pkg.compound_array_count, sizeof(*pkg.compound_array));
    static const char *names[] = { "alpha", "beta", "gamma", "delta", "epsilon" };
    for (size_t i = 0; i < pkg.compound_array_count; ++i) {
        pkg.compound_array[i].id = (uint32_t)(seed * 1103u + (unsigned)i);
        const char *nm = names[(seed + i) % (sizeof(names) / sizeof(names[0]))];
        pkg.compound_array[i].name.bytes = (char *)nm;
        pkg.compound_array[i].name.length = strlen(nm);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_variable_array_subtyped_struct_variable_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.compound_array); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%u bytes=%zu\n", seed, (unsigned)pkg.num_elements, byte_pos);
    free(pkg.compound_array);
    return 0;
}
