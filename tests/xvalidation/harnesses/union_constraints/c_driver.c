/* Union with a per-arm constraint expression. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "constraints/union_constraints/union_constraints.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    constraints_union_constraints_union_constraints_t pkg;
    constraints_union_constraints_union_constraints_init(&pkg);

    if (seed & 1u) {
        pkg.tag = constraints_union_constraints_UNION_CONSTRAINTS_TAG_VALUE8;
        pkg.u.value8 = (uint8_t)((seed % 250u) + 1u); /* != 0 */
    } else {
        pkg.tag = constraints_union_constraints_UNION_CONSTRAINTS_TAG_VALUE16;
        pkg.u.value16 = (uint16_t)((seed * 7u) | 0x100u); /* > 255 */
    }

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = constraints_union_constraints_union_constraints_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
