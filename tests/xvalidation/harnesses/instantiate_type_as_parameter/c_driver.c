/* Template-instantiated struct used as a struct parameter. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "templates/instantiate_type_as_parameter/instantiate_type_as_parameter.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_instantiate_type_as_parameter_instantiate_type_as_parameter_t pkg;
    templates_instantiate_type_as_parameter_instantiate_type_as_parameter_init(&pkg);

    pkg.parameter.value = (uint32_t)((seed % 5u) + 2u);
    pkg.parameterized.arr_count = (size_t)pkg.parameter.value;
    pkg.parameterized.arr = (uint32_t *)calloc(pkg.parameterized.arr_count, sizeof(uint32_t));
    for (size_t i = 0; i < pkg.parameterized.arr_count; ++i)
        pkg.parameterized.arr[i] = (uint32_t)(seed * 7u + (unsigned)i);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_instantiate_type_as_parameter_instantiate_type_as_parameter_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
