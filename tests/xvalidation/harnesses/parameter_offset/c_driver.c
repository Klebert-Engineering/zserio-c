/* Offset SOURCE field (`offsetHolder.roomOffset`) lives in a struct
 * passed by parameter to the offset's owning struct (Room). Our
 * generator emits the read-side validation + write-side byte alignment
 * but doesn't back-patch through a parameter compound — the caller
 * pre-sets the value (matching upstream cpp's two-pass approach).
 *
 * Wire layout: schoolId (uint16) + offsetHolder (uint32) + roomId
 * (uint16, byte-aligned). roomId starts at byte 6 → offsetHolder.
 * roomOffset = 6. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/parameter_offset/school.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_parameter_offset_school_t pkg;
    offsets_parameter_offset_school_init(&pkg);

    pkg.school_id = (uint16_t)(seed % 65535u);
    pkg.offset_holder.room_offset = 6u; /* byte position of roomId */
    pkg.room.room_id = (uint16_t)((seed * 19u) & 0xFFFFu);
    /* Room reads offsetHolder via its parameter — keep the parameter
     * struct in sync with what the parent will emit on the wire. */
    pkg.room.offset_holder = pkg.offset_holder;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_parameter_offset_school_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
