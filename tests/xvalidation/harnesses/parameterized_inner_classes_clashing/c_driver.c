/* Struct named `ElementChildrenInitializer_array` — clashes with the
 * generator's internal helper-class naming pattern for arrays of
 * parameterized compounds. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/parameterized_inner_classes_clashing/element_children_initializer_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_parameterized_inner_classes_clashing_element_children_initializer_array_t pkg;
    parameterized_types_parameterized_inner_classes_clashing_element_children_initializer_array_init(&pkg);

    pkg.array_count = (size_t)((seed % 3u) + 2u);
    pkg.array = (parameterized_types_parameterized_inner_classes_clashing_parent_t *)calloc(
            pkg.array_count, sizeof(*pkg.array));
    for (size_t i = 0; i < pkg.array_count; ++i)
    {
        parameterized_types_parameterized_inner_classes_clashing_parent_init(&pkg.array[i]);
        pkg.array[i].param = (uint32_t)(seed + (unsigned)i + 1000u);
        pkg.array[i].compound.field = pkg.array[i].param - 1u; /* < param */
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_parameterized_inner_classes_clashing_element_children_initializer_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
