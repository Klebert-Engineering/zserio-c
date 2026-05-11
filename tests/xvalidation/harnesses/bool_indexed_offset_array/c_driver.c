/* Indexed offset over a bool[5] array. Same pattern as
 * bit5_indexed_offset_array but with bool elements. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "indexed_offsets/bool_indexed_offset_array/bool_indexed_offset_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    indexed_offsets_bool_indexed_offset_array_bool_indexed_offset_array_t pkg;
    indexed_offsets_bool_indexed_offset_array_bool_indexed_offset_array_init(&pkg);

    pkg.spacer = 1u;
    for (size_t i = 0; i < 5; ++i) {
        pkg.data[i] = ((seed + (unsigned)i) & 1u) != 0u;
    }
    /* offsets[] left at zero — writer back-patches per element. */

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = indexed_offsets_bool_indexed_offset_array_bool_indexed_offset_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
