/* uint64 single-field offset with a variable-length byte-array spacer. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/uint64_offset/u_int64_offset.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_uint64_offset_u_int64_offset_t pkg;
    offsets_uint64_offset_u_int64_offset_init(&pkg);

    pkg.array_count = (size_t)((seed % 5u) + 3u);
    pkg.array = (int8_t *)calloc(pkg.array_count, sizeof(int8_t));
    for (size_t i = 0; i < pkg.array_count; ++i)
        pkg.array[i] = (int8_t)((int)(seed + (unsigned)i * 7u) - 30);
    pkg.value = (int32_t)(seed * 991u);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_uint64_offset_u_int64_offset_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
