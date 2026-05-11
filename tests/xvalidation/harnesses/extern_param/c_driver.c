/* `extern` as a struct parameter. Inner struct's wire format is just
 * uint32 field (extern param not on wire). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/extern_param/extern_param.h"

static const uint8_t kBits[] = { 0xAB, 0xCD };

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_extern_param_extern_param_t pkg;
    parameterized_types_extern_param_extern_param_init(&pkg);

    pkg.extern_field.bytes = (uint8_t *)kBits;
    pkg.extern_field.bit_length = 13u;
    pkg.parameterized_field.param = pkg.extern_field;
    pkg.parameterized_field.field = 0xDEADBEEFu ^ seed;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_extern_param_extern_param_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
