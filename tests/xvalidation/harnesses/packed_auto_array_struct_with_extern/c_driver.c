/* Packed array of struct that contains a non-packable extern field. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/extern.h>
#include "array_types/packed_auto_array_struct_with_extern/packed_auto_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_packed_auto_array_struct_with_extern_packed_auto_array_t pkg;
    array_types_packed_auto_array_struct_with_extern_packed_auto_array_init(&pkg);

    pkg.array_count = (size_t)((seed % 3u) + 2u);
    pkg.array = (array_types_packed_auto_array_struct_with_extern_test_structure_t *)calloc(
            pkg.array_count, sizeof(*pkg.array));
    static uint8_t ext_data[8][4];
    for (size_t i = 0; i < pkg.array_count && i < 8; ++i) {
        pkg.array[i].uint32_field = (uint32_t)(seed * 7u + (unsigned)i);
        ext_data[i][0] = (uint8_t)((seed + (unsigned)i * 11u) & 0xFFu);
        ext_data[i][1] = (uint8_t)((seed + (unsigned)i * 13u) & 0xFFu);
        pkg.array[i].extern_field.bytes = ext_data[i];
        pkg.array[i].extern_field.bit_length = 13u; /* odd width */
        pkg.array[i].uint8_field = (uint8_t)((seed + (unsigned)i * 17u) & 0xFFu);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_packed_auto_array_struct_with_extern_packed_auto_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
