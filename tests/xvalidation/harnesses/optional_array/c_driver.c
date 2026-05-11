/* Branching optionals + auto-optional arrays:
 *   bool hasData8
 *   Data8 data8[]      if hasData8
 *   optional Data8 autoData8[]
 *   int16 data16[]     if !hasData8
 *   optional int16 autoData16[]
 *
 * Auto-optional uses a 1-bit presence prefix. The conditional
 * arrays (`if` clause) have no presence prefix — their presence
 * follows from hasData8. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "optional_members/optional_array/test_struct.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    optional_members_optional_array_test_struct_t pkg;
    optional_members_optional_array_test_struct_init(&pkg);

    pkg.has_data8 = (seed & 1u) != 0u;
    if (pkg.has_data8) {
        pkg.data8_present = true;
        pkg.data8_count = (size_t)((seed % 3u) + 1u);
        pkg.data8 = (optional_members_optional_array_data8_t *)calloc(
                pkg.data8_count, sizeof(*pkg.data8));
        for (size_t i = 0; i < pkg.data8_count; ++i)
            pkg.data8[i].data = (int8_t)((int)((seed + (unsigned)i) & 0x7fu));
    } else {
        pkg.data16_present = true;
        pkg.data16_count = (size_t)((seed % 4u) + 1u);
        pkg.data16 = (int16_t *)calloc(pkg.data16_count, sizeof(*pkg.data16));
        for (size_t i = 0; i < pkg.data16_count; ++i)
            pkg.data16[i] = (int16_t)((int)(seed * 17u + (unsigned)i) - 1024);
    }

    /* Auto-optional arrays: present iff seed bit set. */
    if ((seed >> 1) & 1u) {
        pkg.auto_data8_present = true;
        pkg.auto_data8_count = 2;
        pkg.auto_data8 = (optional_members_optional_array_data8_t *)calloc(2, sizeof(*pkg.auto_data8));
        pkg.auto_data8[0].data = 5;
        pkg.auto_data8[1].data = -3;
    }
    if ((seed >> 2) & 1u) {
        pkg.auto_data16_present = true;
        pkg.auto_data16_count = 3;
        pkg.auto_data16 = (int16_t *)calloc(3, sizeof(*pkg.auto_data16));
        pkg.auto_data16[0] = 100; pkg.auto_data16[1] = -200; pkg.auto_data16[2] = 0x4242;
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = optional_members_optional_array_test_struct_write(&pkg, &w);
    if (st != ZSERIO_OK) {
        fprintf(stderr, "write: %d\n", (int)st);
        free(pkg.data8); free(pkg.data16); free(pkg.auto_data8); free(pkg.auto_data16);
        return 1;
    }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u has_data8=%d bytes=%zu\n",
            seed, (int)pkg.has_data8, byte_pos);
    free(pkg.data8); free(pkg.data16); free(pkg.auto_data8); free(pkg.auto_data16);
    return 0;
}
