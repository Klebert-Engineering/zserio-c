/* `extend string array[]` with `offsets[@index]:` directive — combines
 * extend (byte-align), indexed offsets (per-element back-patch), and
 * the string element type all in one schema. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "extended_members/extended_indexed_offsets/extended.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_extended_indexed_offsets_extended_t pkg;
    extended_members_extended_indexed_offsets_extended_init(&pkg);

    const size_t n = (size_t)((seed % 3u) + 2u);
    pkg.offsets_count = n;
    pkg.offsets = (uint32_t *)calloc(n, sizeof(uint32_t));
    /* offsets[] left at zero — writer back-patches per element. */
    pkg.array_count = n;
    pkg.array = (zserio_string_t *)calloc(n, sizeof(zserio_string_t));
    static const char *kStrings[] = { "alpha", "beta", "gamma", "delta", "epsilon" };
    for (size_t i = 0; i < n; ++i) {
        const char *s = kStrings[(seed + i) % (sizeof(kStrings) / sizeof(kStrings[0]))];
        pkg.array[i].bytes = (char *)s;
        pkg.array[i].length = strlen(s);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_extended_indexed_offsets_extended_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%zu bytes=%zu\n", seed, n, byte_pos);
    return 0;
}
