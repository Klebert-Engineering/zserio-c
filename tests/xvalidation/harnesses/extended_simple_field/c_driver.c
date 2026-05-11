/* `extend` keyword adds a field that the original wire format
 * didn't have — a backward-compatibility mechanism. Round-tripping
 * with both fields present writes them in declaration order; the
 * wire output should match upstream. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "extended_members/extended_simple_field/extended.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_extended_simple_field_extended_t pkg;
    extended_members_extended_simple_field_extended_init(&pkg);
    pkg.value = 0xCAFEF00Du ^ seed;
    pkg.extended_value = 0x0123456789ABCDEFull ^ ((uint64_t)seed << 32);

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_extended_simple_field_extended_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
