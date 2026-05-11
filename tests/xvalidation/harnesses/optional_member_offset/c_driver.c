/* Offset directive whose target is an optional field (`if hasOptional == true`). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/optional_member_offset/optional_member_offset.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_optional_member_offset_optional_member_offset_t pkg;
    offsets_optional_member_offset_optional_member_offset_init(&pkg);

    pkg.has_optional = (seed & 1u) ? true : false;
    if (pkg.has_optional)
    {
        pkg.optional_field = (int32_t)(seed * 17u + 42u);
        pkg.optional_field_present = true;
    }
    pkg.field = (int32_t)(seed * 31u);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_optional_member_offset_optional_member_offset_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
