/* Offset source `unionField.offset` lives in a sibling Union arm.
 * Wire layout: varsize tag (1 byte = 0) + uint32 offset (32 bits) +
 * align (0 bits, already byte-aligned) + u32Field (32 bits). u32Field
 * starts at byte 5 → offset = 5. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/offset_in_union/test_struct.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_offset_in_union_test_struct_t pkg;
    offsets_offset_in_union_test_struct_init(&pkg);

    pkg.union_field.tag = offsets_offset_in_union_TEST_UNION_TAG_OFFSET;
    pkg.union_field.u.offset = 5u; /* byte position of u32Field */
    pkg.u32_field = (uint32_t)(seed * 1103u + 7u);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_offset_in_union_test_struct_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
