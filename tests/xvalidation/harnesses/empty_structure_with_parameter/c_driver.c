/* Empty struct with a parameter — encodes 0 bytes. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "structure_types/empty_structure_with_parameter/empty_structure_with_parameter.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    structure_types_empty_structure_with_parameter_empty_structure_with_parameter_t pkg;
    structure_types_empty_structure_with_parameter_empty_structure_with_parameter_init(&pkg);
    structure_types_empty_structure_with_parameter_empty_structure_with_parameter_set_parameters(&pkg,
            (int32_t)(seed * 13u));

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = structure_types_empty_structure_with_parameter_empty_structure_with_parameter_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
