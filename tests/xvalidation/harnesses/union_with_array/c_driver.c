/* Union whose arms are arrays. The active arm's count is filled in;
 * the inactive arm's count is left at zero (matches upstream cpp's
 * Vector-default behaviour). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "union_types/union_with_array/test_union.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    union_types_union_with_array_test_union_t pkg;
    union_types_union_with_array_test_union_init(&pkg);

    const unsigned arm = seed & 1u;
    pkg.tag = arm;
    if (arm == 0u) {
        pkg.array8_count = (size_t)((seed % 4u) + 1u);
        pkg.u.array8 = (union_types_union_with_array_data8_t *)calloc(
                pkg.array8_count, sizeof(*pkg.u.array8));
        for (size_t i = 0; i < pkg.array8_count; ++i)
            pkg.u.array8[i].data = (int8_t)((int)((seed + (unsigned)i) & 0x7fu));
    } else {
        pkg.array16_count = (size_t)((seed % 5u) + 1u);
        pkg.u.array16 = (int16_t *)calloc(pkg.array16_count, sizeof(*pkg.u.array16));
        for (size_t i = 0; i < pkg.array16_count; ++i)
            pkg.u.array16[i] = (int16_t)((int)(seed * 17u + (unsigned)i) - 256);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = union_types_union_with_array_test_union_write(&pkg, &w);
    if (st != ZSERIO_OK) {
        fprintf(stderr, "write: %d\n", (int)st);
        if (arm == 0u) free(pkg.u.array8); else free(pkg.u.array16);
        return 1;
    }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u arm=%u bytes=%zu\n", seed, arm, byte_pos);
    if (arm == 0u) free(pkg.u.array8); else free(pkg.u.array16);
    return 0;
}
