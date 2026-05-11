/* Packed auto-array of struct containing a non-packable bytes field
 * sandwiched between two packable ints. Packing context covers only
 * uint32_field and uint8_field; bytes_field falls through to the
 * unpacked write path. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/packed_auto_array_struct_with_bytes/packed_auto_array.h"

static const uint8_t kPayloadA[] = { 0x11, 0x22, 0x33 };
static const uint8_t kPayloadB[] = { 0xAA };

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_packed_auto_array_struct_with_bytes_packed_auto_array_t pkg;
    array_types_packed_auto_array_struct_with_bytes_packed_auto_array_init(&pkg);

    pkg.array_count = (size_t)((seed % 4u) + 2u);
    pkg.array = (array_types_packed_auto_array_struct_with_bytes_test_structure_t *)calloc(
            pkg.array_count, sizeof(*pkg.array));
    for (size_t i = 0; i < pkg.array_count; ++i) {
        pkg.array[i].uint32_field = (uint32_t)(seed + (unsigned)i * 100u);
        pkg.array[i].bytes_field.bytes = (uint8_t *)((i & 1u) ? kPayloadB : kPayloadA);
        pkg.array[i].bytes_field.length = (i & 1u) ? sizeof(kPayloadB) : sizeof(kPayloadA);
        pkg.array[i].uint8_field = (uint8_t)((seed + (unsigned)i) & 0xFFu);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_packed_auto_array_struct_with_bytes_packed_auto_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.array); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%zu bytes=%zu\n", seed, pkg.array_count, byte_pos);
    free(pkg.array);
    return 0;
}
