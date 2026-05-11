/* Template instantiated as a named type (Test32 = Test<uint32>),
 * then used as the element type of an auto-array. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "templates/instantiate_type_as_struct_array_field/instantiate_type_as_struct_array_field.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_instantiate_type_as_struct_array_field_instantiate_type_as_struct_array_field_t pkg;
    templates_instantiate_type_as_struct_array_field_instantiate_type_as_struct_array_field_init(&pkg);

    pkg.test_count = (size_t)((seed % 4u) + 2u);
    pkg.test = (templates_instantiate_type_as_struct_array_field_test32_t *)calloc(
            pkg.test_count, sizeof(*pkg.test));
    for (size_t i = 0; i < pkg.test_count; ++i)
        pkg.test[i].value = (uint32_t)(seed * 1103u + (unsigned)i);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_instantiate_type_as_struct_array_field_instantiate_type_as_struct_array_field_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
