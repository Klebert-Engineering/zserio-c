/* `bytes` as a struct parameter — the inner struct's wire format is
 * just `uint32 field` (the bytes param isn't on the wire). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/bytes_param/bytes_param.h"

static const uint8_t kBytes[] = { 0x01, 0x02, 0x03, 0x04 };

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_bytes_param_bytes_param_t pkg;
    parameterized_types_bytes_param_bytes_param_init(&pkg);

    pkg.bytes_field.bytes = (uint8_t *)kBytes;
    pkg.bytes_field.length = sizeof(kBytes);
    pkg.parameterized_field.param = pkg.bytes_field;
    pkg.parameterized_field.field = 0xDEADBEEFu ^ seed;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_bytes_param_bytes_param_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
