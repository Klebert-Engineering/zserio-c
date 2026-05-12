/* Top-level struct named `ArrayType_array` — the `_array` suffix
 * deliberately clashes with the generator's internal "inner classes"
 * suffix for arrays of the same field name. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "structure_types/structure_inner_classes_clashing/array_type_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    structure_types_structure_inner_classes_clashing_array_type_array_t pkg;
    structure_types_structure_inner_classes_clashing_array_type_array_init(&pkg);

    pkg.array_count = (size_t)((seed % 4u) + 2u);
    pkg.array = (uint32_t *)calloc(pkg.array_count, sizeof(uint32_t));
    for (size_t i = 0; i < pkg.array_count; ++i)
        pkg.array[i] = (uint32_t)(seed * 191u + (unsigned)i);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = structure_types_structure_inner_classes_clashing_array_type_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
