/* Fixed-length indexed-offset arrays where the anchored element is
 * a compound type (Compound { uint32 id; bit:3 value; }). Each
 * compound element is byte-aligned via `offsets[@index]:`. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "indexed_offsets/compound_indexed_offset_array/compound_indexed_offset_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    indexed_offsets_compound_indexed_offset_array_compound_indexed_offset_array_t pkg;
    indexed_offsets_compound_indexed_offset_array_compound_indexed_offset_array_init(&pkg);

    pkg.spacer = 1u;
    for (size_t i = 0; i < 5; ++i) {
        pkg.data[i].id = (uint32_t)(seed * 1103u + (unsigned)i);
        pkg.data[i].value = (uint8_t)((seed + (unsigned)i) & 0x07u);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = indexed_offsets_compound_indexed_offset_array_compound_indexed_offset_array_write(&pkg, &w);
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
