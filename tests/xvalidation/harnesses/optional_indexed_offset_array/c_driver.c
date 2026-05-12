/* Indexed-offset array whose target is an optional string array
 * gated by `if hasOptional == true`. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "indexed_offsets/optional_indexed_offset_array/optional_indexed_offset_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    indexed_offsets_optional_indexed_offset_array_optional_indexed_offset_array_t pkg;
    indexed_offsets_optional_indexed_offset_array_optional_indexed_offset_array_init(&pkg);

    pkg.has_optional = (seed & 1u) ? true : false;
    if (pkg.has_optional)
    {
        static char text[5][32];
        for (int i = 0; i < 5; ++i)
        {
            int n = snprintf(text[i], sizeof(text[i]), "row-%u-%d", seed, i);
            pkg.data[i].bytes = text[i];
            pkg.data[i].length = (size_t)n;
        }
        pkg.data_present = true;
    }
    pkg.field = (uint8_t)((seed * 3u) & 0x3Fu);

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = indexed_offsets_optional_indexed_offset_array_optional_indexed_offset_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u has_opt=%d bytes=%zu\n", seed, (int)pkg.has_optional, byte_pos);
    return 0;
}
