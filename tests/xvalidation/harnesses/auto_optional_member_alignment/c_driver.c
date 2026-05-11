/* `align(32):` before an auto-optional int32. Driver flips
 * presence via seed. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "alignment/auto_optional_member_alignment/auto_optional_member_alignment.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    alignment_auto_optional_member_alignment_auto_optional_member_alignment_t pkg;
    alignment_auto_optional_member_alignment_auto_optional_member_alignment_init(&pkg);

    if (seed & 1u) {
        pkg.auto_optional_field_present = true;
        pkg.auto_optional_field = (int32_t)(seed * 17u);
    }
    pkg.field = (int32_t)(seed * 1103u);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = alignment_auto_optional_member_alignment_auto_optional_member_alignment_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
