/* Offset directives: the eight `field<N>Offset` fields hold the
 * byte position at which the corresponding `fieldN` starts. Each
 * anchored field is byte-aligned. The driver doesn't have to set
 * the offset values — the generator's single-pass back-patch
 * updates them in memory and rewrites them at the recorded wire
 * position before writing the anchored field. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/bit_offset/bit_offset.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_bit_offset_bit_offset_t pkg;
    offsets_bit_offset_bit_offset_init(&pkg);

    /* Anchored fields each carry the low N bits of the seed. The
     * offset fields are left at zero — _write back-patches them. */
    pkg.field1 = (uint8_t)((seed >> 0) & 0x01u);
    pkg.field2 = (uint8_t)((seed >> 1) & 0x03u);
    pkg.field3 = (uint8_t)((seed >> 2) & 0x07u);
    pkg.field4 = (uint8_t)((seed >> 3) & 0x0Fu);
    pkg.field5 = (uint8_t)((seed >> 4) & 0x1Fu);
    pkg.field6 = (uint8_t)((seed >> 5) & 0x3Fu);
    pkg.field7 = (uint8_t)((seed >> 6) & 0x7Fu);
    pkg.field8 = (uint8_t)((seed >> 7) & 0xFFu);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_bit_offset_bit_offset_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu offs=[%u,%u,%u,%u,%u,%u,%u,%u]\n",
            seed, byte_pos,
            (unsigned)pkg.field1_offset, (unsigned)pkg.field2_offset,
            (unsigned)pkg.field3_offset, (unsigned)pkg.field4_offset,
            (unsigned)pkg.field5_offset, (unsigned)pkg.field6_offset,
            (unsigned)pkg.field7_offset, (unsigned)pkg.field8_offset);
    return 0;
}
