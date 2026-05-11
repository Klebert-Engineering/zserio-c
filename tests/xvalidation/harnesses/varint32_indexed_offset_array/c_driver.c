/* Indexed-offset array with varint32 elements (variable byte width). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "indexed_offsets/varint32_indexed_offset_array/var_int32_indexed_offset_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    indexed_offsets_varint32_indexed_offset_array_var_int32_indexed_offset_array_t pkg;
    indexed_offsets_varint32_indexed_offset_array_var_int32_indexed_offset_array_init(&pkg);

    pkg.data[0] = (int32_t)(seed % 60u);
    pkg.data[1] = (int32_t)(seed * 7u + 100u);
    pkg.data[2] = (int32_t)(-(int)(seed * 3u + 500u));
    pkg.data[3] = (int32_t)(seed * 991u);
    pkg.data[4] = (int32_t)(-(int)(seed * 13u + 70000));
    pkg.spacer = 1u;

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = indexed_offsets_varint32_indexed_offset_array_var_int32_indexed_offset_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
