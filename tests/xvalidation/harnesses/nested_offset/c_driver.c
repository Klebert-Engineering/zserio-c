/* Nested offsets: outer NestedOffset has its own `terminatorOffset:`
 * directive plus a Choice that may carry a Union with an array of
 * NestedOffsetStructure (each with its own `dataOffset:`). We pick
 * the `dummy` arm (NestedOffsetChoice on type=false → uint8 dummy)
 * to keep the driver brief; the outer offset wrap still runs. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/nested_offset/nested_offset.h"
#include "offsets/nested_offset/nested_offset_choice.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_nested_offset_nested_offset_t pkg;
    offsets_nested_offset_nested_offset_init(&pkg);

    pkg.bool_value = false;
    offsets_nested_offset_nested_offset_choice_set_parameters(&pkg.nested_offset_choice, false);
    pkg.nested_offset_choice.u.dummy = (uint8_t)(seed & 0xFFu);
    pkg.terminator = (uint8_t)((seed >> 1) & 0x7Fu);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_nested_offset_nested_offset_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
