/* Fixed-length indexed-offset arrays: offsets[5] (uint32) and
 * data[5] (bit:5), with `offsets[@index]:` byte-aligning each
 * data element. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "indexed_offsets/bit5_indexed_offset_array/bit5_indexed_offset_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    indexed_offsets_bit5_indexed_offset_array_bit5_indexed_offset_array_t pkg;
    indexed_offsets_bit5_indexed_offset_array_bit5_indexed_offset_array_init(&pkg);

    pkg.spacer = 1u;
    for (size_t i = 0; i < 5; ++i) {
        pkg.data[i] = (uint8_t)((seed * 17u + (unsigned)i) & 0x1Fu);
    }
    /* Offsets left at zero — writer back-patches them. */

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = indexed_offsets_bit5_indexed_offset_array_bit5_indexed_offset_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu offs=[%u,%u,%u,%u,%u]\n",
            seed, byte_pos,
            (unsigned)pkg.offsets[0], (unsigned)pkg.offsets[1],
            (unsigned)pkg.offsets[2], (unsigned)pkg.offsets[3],
            (unsigned)pkg.offsets[4]);
    return 0;
}
