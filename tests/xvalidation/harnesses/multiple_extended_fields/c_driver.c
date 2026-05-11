/* Two sequential `extend` fields; second has a schema default value.
 * Wire format: value(32b) + align(8) + extended_value1(4b) + align(8)
 * + extended_value2 string. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "extended_members/multiple_extended_fields/extended2.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_multiple_extended_fields_extended2_t pkg;
    extended_members_multiple_extended_fields_extended2_init(&pkg);
    pkg.value = 0xDEADBEEFu ^ seed;
    pkg.extended_value1 = (uint8_t)((seed * 5u) & 0x0Fu);
    const char *s = (seed & 1u) ? "alpha" : "extended";
    pkg.extended_value2.bytes = (char *)s;
    pkg.extended_value2.length = strlen(s);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_multiple_extended_fields_extended2_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
