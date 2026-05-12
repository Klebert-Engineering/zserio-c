/* Offset source `choiceField.offset` lives in a sibling Choice arm.
 * Generator emits read-validate + write-align for the dotted source;
 * the c_driver pre-computes the byte position of u32Field.
 *
 * Wire layout when hasOffset=true: bit hasOffset (1 bit) + choice arm
 * uint32 offset (32 bits) + align to byte (7 bits padding) + u32Field
 * (32 bits). u32Field starts at bit 40 → byte 5. So offset = 5. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/offset_in_choice/test_struct.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_offset_in_choice_test_struct_t pkg;
    offsets_offset_in_choice_test_struct_init(&pkg);

    pkg.has_offset = true;
    pkg.choice_field.u.offset = 5u; /* byte position of u32Field */
    pkg.u32_field = (uint32_t)(seed * 1103u);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_offset_in_choice_test_struct_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
