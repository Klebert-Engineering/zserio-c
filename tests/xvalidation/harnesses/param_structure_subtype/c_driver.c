/* Subtype-of-subtype of a parameterised struct, used both as a field
 * and as an auto-array element type. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "subtypes/param_structure_subtype/parameterized_subtype_struct.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    subtypes_param_structure_subtype_parameterized_subtype_struct_t pkg;
    subtypes_param_structure_subtype_parameterized_subtype_struct_init(&pkg);

    pkg.length = (int32_t)((seed % 4u) + 2u);
    pkg.parameterized_subtype.array_count = (size_t)pkg.length;
    pkg.parameterized_subtype.array = (int32_t *)calloc(pkg.parameterized_subtype.array_count, sizeof(int32_t));
    for (size_t i = 0; i < pkg.parameterized_subtype.array_count; ++i)
        pkg.parameterized_subtype.array[i] = (int32_t)(seed * 11u + (unsigned)i);

    pkg.another_parameterized_subtype_array_count = 2u;
    pkg.another_parameterized_subtype_array = (subtypes_param_structure_subtype_parameterized_struct_t *)calloc(2,
            sizeof(*pkg.another_parameterized_subtype_array));
    for (size_t e = 0; e < 2; ++e)
    {
        pkg.another_parameterized_subtype_array[e].array_count = (size_t)pkg.length;
        pkg.another_parameterized_subtype_array[e].array = (int32_t *)calloc((size_t)pkg.length, sizeof(int32_t));
        for (size_t i = 0; i < (size_t)pkg.length; ++i)
            pkg.another_parameterized_subtype_array[e].array[i] = (int32_t)(seed * 13u + (unsigned)(e * 100 + i));
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = subtypes_param_structure_subtype_parameterized_subtype_struct_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
