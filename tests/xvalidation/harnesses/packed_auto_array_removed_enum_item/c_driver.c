/* Packed array of an enum where one item is @removed. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/packed_auto_array_removed_enum_item/packed_auto_array_removed_enum_item.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_packed_auto_array_removed_enum_item_packed_auto_array_removed_enum_item_t pkg;
    array_types_packed_auto_array_removed_enum_item_packed_auto_array_removed_enum_item_init(&pkg);

    /* Avoid the @removed HEAVY (value 2): cycle through NONE=1, LIGHT=3, MID=4. */
    static const uint8_t legal[] = {1u, 3u, 4u};
    pkg.packed_array_count = (size_t)((seed % 4u) + 3u);
    pkg.packed_array = (array_types_packed_auto_array_removed_enum_item_traffic_t *)calloc(
            pkg.packed_array_count, sizeof(*pkg.packed_array));
    for (size_t i = 0; i < pkg.packed_array_count; ++i)
        pkg.packed_array[i] = (array_types_packed_auto_array_removed_enum_item_traffic_t)
                legal[(seed + (unsigned)i) % 3u];

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_packed_auto_array_removed_enum_item_packed_auto_array_removed_enum_item_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
