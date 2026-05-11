/* Mixed packed/unpacked arrays of multi-Field compounds — the inner
 * compound has the same arity as a possible constructor signature. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "structure_types/field_constructor_clashing/field_constructor_clashing.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    structure_types_field_constructor_clashing_field_constructor_clashing_t pkg;
    structure_types_field_constructor_clashing_field_constructor_clashing_init(&pkg);

    pkg.compound_read_array_count = (size_t)((seed % 3u) + 2u);
    pkg.compound_read_array = (structure_types_field_constructor_clashing_compound_read_t *)calloc(
            pkg.compound_read_array_count, sizeof(*pkg.compound_read_array));
    for (size_t i = 0; i < pkg.compound_read_array_count; ++i)
    {
        pkg.compound_read_array[i].field1.value = (uint32_t)(seed * 11u + (unsigned)i);
        pkg.compound_read_array[i].field2.value = (uint32_t)(seed * 13u + (unsigned)i);
    }

    pkg.compound_packing_array_count = (size_t)((seed % 3u) + 2u);
    pkg.compound_packing_array = (structure_types_field_constructor_clashing_compound_packing_read_t *)calloc(
            pkg.compound_packing_array_count, sizeof(*pkg.compound_packing_array));
    for (size_t i = 0; i < pkg.compound_packing_array_count; ++i)
    {
        pkg.compound_packing_array[i].field1.value = (uint32_t)(seed * 17u + (unsigned)i);
        pkg.compound_packing_array[i].field2.value = (uint32_t)(seed * 19u + (unsigned)i);
        pkg.compound_packing_array[i].field3.value = (uint32_t)(seed * 23u + (unsigned)i);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = structure_types_field_constructor_clashing_field_constructor_clashing_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
