/* Union with 4 arms (int8, uint16, string, int8) — exercises the union
 * tag wire form (varsize) and string arm. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "union_types/simple_union/simple_union.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    union_types_simple_union_simple_union_t pkg;
    union_types_simple_union_simple_union_init(&pkg);

    const unsigned arm = seed % 4u;
    pkg.tag = arm;
    switch (arm) {
        case 0u: pkg.u.case1_field = (int8_t)((seed * 17u) & 0x7fu); break;
        case 1u: pkg.u.case2_field = (uint16_t)((seed * 1103u) + 7u); break;
        case 2u: {
            const char *s = (seed & 1u) ? "union-case3" : "third-arm";
            pkg.u.case3_field.bytes = (char *)s;
            pkg.u.case3_field.length = strlen(s);
            break;
        }
        case 3u: pkg.u.case4_field = (int8_t)(-(int)((seed * 5u) & 0x3fu)); break;
        default: break;
    }

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = union_types_simple_union_simple_union_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u arm=%u bytes=%zu\n", seed, arm, byte_pos);
    return 0;
}
