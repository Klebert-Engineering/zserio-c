/* Indexed offsets feeding a packed bit:5 array. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "indexed_offsets/packed_auto_indexed_offset_array/auto_indexed_offset_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    indexed_offsets_packed_auto_indexed_offset_array_auto_indexed_offset_array_t pkg;
    indexed_offsets_packed_auto_indexed_offset_array_auto_indexed_offset_array_init(&pkg);

    const size_t n = (size_t)((seed % 5u) + 3u);
    pkg.data_count = n;
    pkg.data = (uint8_t *)calloc(n, sizeof(uint8_t));
    /* offsets[] back-patched; the writer just needs an array of length n. */
    pkg.offsets_count = n;
    pkg.offsets = (uint32_t *)calloc(n, sizeof(uint32_t));
    for (size_t i = 0; i < n; ++i)
        pkg.data[i] = (uint8_t)((seed + (unsigned)i) & 0x1Fu);
    pkg.spacer = 1u;

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = indexed_offsets_packed_auto_indexed_offset_array_auto_indexed_offset_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
