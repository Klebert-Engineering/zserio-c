/* See packed_variable_array_uint8/c_driver.c for the harness pattern.
 * This exercises a packed array of structs with a packed scalar +
 * an unpacked varuint sibling — i.e. the wire format must combine
 * packed encoding for the uint8 leaf with plain varuint for the
 * varuint leaf, in each element. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "array_types/packed_auto_array_struct_with_unpacked_field/packed_auto_array.h"

typedef array_types_packed_auto_array_struct_with_unpacked_field_packed_auto_array_t pkg_t;
typedef array_types_packed_auto_array_struct_with_unpacked_field_test_structure_t   elt_t;

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);

    pkg_t pkg;
    array_types_packed_auto_array_struct_with_unpacked_field_packed_auto_array_init(&pkg);
    const size_t count = (size_t)((seed % 12U) + 1U);
    pkg.array_count = count;
    pkg.array = calloc(count, sizeof(elt_t));
    uint32_t s = seed * 2654435761u + 1u;
    for (size_t i = 0; i < count; ++i) {
        s = s * 1103515245u + 12345u;
        pkg.array[i].uint8_field = (uint8_t)((s >> 16) & 0xffu);
        s = s * 1103515245u + 12345u;
        /* Keep varuint values small enough to fit in 1-3 bytes —
         * exercises the "smart packing" path without exotic sizes. */
        pkg.array[i].unpacked_field = (uint64_t)((s >> 8) & 0x3fffu);
    }

    uint8_t buf[1024];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st =
        array_types_packed_auto_array_struct_with_unpacked_field_packed_auto_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;

    FILE *f = fopen(argv[2], "wb");
    fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u count=%zu bytes=%zu\n", seed, count, byte_pos);

    free(pkg.array);
    return 0;
}
