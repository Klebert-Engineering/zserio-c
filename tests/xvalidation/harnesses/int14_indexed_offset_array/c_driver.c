/* Indexed-offset array with int<14> signed bitfield elements. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "indexed_offsets/int14_indexed_offset_array/int14_indexed_offset_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    indexed_offsets_int14_indexed_offset_array_int14_indexed_offset_array_t pkg;
    indexed_offsets_int14_indexed_offset_array_int14_indexed_offset_array_init(&pkg);

    /* offsets[] back-patched by the encoder; spacer + 5 int14 elements. */
    for (int i = 0; i < 5; ++i)
        pkg.data[i] = (int16_t)((int)(seed + (unsigned)i * 17u) - 4000);
    pkg.spacer = 1u;

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = indexed_offsets_int14_indexed_offset_array_int14_indexed_offset_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
