/* Atomic offset followed by a packed auto-array of signed 7-bit ints. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/packed_auto_array_offset/auto_array_holder.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_packed_auto_array_offset_auto_array_holder_t pkg;
    offsets_packed_auto_array_offset_auto_array_holder_init(&pkg);

    pkg.auto_array_count = (size_t)((seed % 4u) + 3u);
    pkg.auto_array = (int8_t *)calloc(pkg.auto_array_count, sizeof(int8_t));
    for (size_t i = 0; i < pkg.auto_array_count; ++i)
        pkg.auto_array[i] = (int8_t)((int)(seed + (unsigned)i) % 63 - 30);
    pkg.force_alignment = 1u;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_packed_auto_array_offset_auto_array_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
