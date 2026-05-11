/* See packed_variable_array_uint8/c_driver.c for the harness pattern. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "array_types/packed_fixed_array_uint8/packed_fixed_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);

    array_types_packed_fixed_array_uint8_packed_fixed_array_t pkg;
    array_types_packed_fixed_array_uint8_packed_fixed_array_init(&pkg);
    uint32_t s = seed * 2654435761u + 1u;
    for (size_t i = 0; i < 5; ++i) {
        s = s * 1103515245u + 12345u;
        pkg.uint8_array[i] = (uint8_t)((s >> 16) & 0xffu);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_packed_fixed_array_uint8_packed_fixed_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;

    FILE *f = fopen(argv[2], "wb");
    fwrite(buf, 1, byte_pos, f);
    fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
