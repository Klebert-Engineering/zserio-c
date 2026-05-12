/* `packed Element[]` where Element holds a parameterised Parameterized(5).
 * Tests that the packed-compound-array dispatch recurses into a nested
 * parameterised struct subfield. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/parameterized_nested_in_array/holder.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_parameterized_nested_in_array_holder_t pkg;
    parameterized_types_parameterized_nested_in_array_holder_init(&pkg);

    /* The constraint requires `field > param` (param = 5). */
    pkg.element_array_count = (size_t)((seed % 4u) + 2u);
    pkg.element_array = (parameterized_types_parameterized_nested_in_array_element_t *)calloc(
            pkg.element_array_count, sizeof(*pkg.element_array));
    for (size_t i = 0; i < pkg.element_array_count; ++i)
        pkg.element_array[i].parameterized.field = (uint32_t)(seed * 7u + (unsigned)i + 6u);

    pkg.element_packed_array_count = (size_t)((seed % 4u) + 2u);
    pkg.element_packed_array = (parameterized_types_parameterized_nested_in_array_element_t *)calloc(
            pkg.element_packed_array_count, sizeof(*pkg.element_packed_array));
    for (size_t i = 0; i < pkg.element_packed_array_count; ++i)
        pkg.element_packed_array[i].parameterized.field = (uint32_t)(seed * 11u + (unsigned)i + 6u);

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_parameterized_nested_in_array_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
