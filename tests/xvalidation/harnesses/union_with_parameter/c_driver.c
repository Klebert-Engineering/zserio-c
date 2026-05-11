/* Union with parameter `case1Allowed` that gates arm 0:
 *   int32 case1Field : case1Allowed;
 *   int16 case2Field;
 *   int8  case3Field;
 *
 * Arm 0 only valid when case1Allowed=true. Arms 1 and 2 always
 * allowed. Driver picks an allowed arm based on seed. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "union_types/union_with_parameter/test_union.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const bool case1Allowed = (seed & 1u) != 0u;
    /* If arm 0 disallowed, fall back to arms {1, 2}. */
    unsigned arm;
    if (case1Allowed) {
        arm = seed % 3u;
    } else {
        arm = 1u + (seed % 2u);
    }

    union_types_union_with_parameter_test_union_t pkg;
    union_types_union_with_parameter_test_union_init(&pkg);
    union_types_union_with_parameter_test_union_set_parameters(&pkg, case1Allowed);
    pkg.tag = arm;
    switch (arm) {
        case 0u: pkg.u.case1_field = (int32_t)(seed * 1103u); break;
        case 1u: pkg.u.case2_field = (int16_t)((int)(seed * 7u) - 1024); break;
        case 2u: pkg.u.case3_field = (int8_t)((int)((seed * 11u) & 0xFFu) - 128); break;
        default: break;
    }

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = union_types_union_with_parameter_test_union_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u c1?%d arm=%u bytes=%zu\n",
            seed, (int)case1Allowed, arm, byte_pos);
    return 0;
}
