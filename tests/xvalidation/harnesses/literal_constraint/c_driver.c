/* Literal-bounded int32 constraint:
 *   value in [-268435455, -1] U [1, 268435455]
 * Pick values that satisfy. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "constraints/literal_constraint/literal_constraint.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    constraints_literal_constraint_literal_constraint_t pkg;
    constraints_literal_constraint_literal_constraint_init(&pkg);

    int32_t v = (int32_t)(seed * 1103u + 1u);
    if (v == 0) v = 1;
    if (v < -268435455) v = -268435455;
    if (v >  268435455) v =  268435455;
    if ((seed & 1u) != 0u) v = -v;
    if (v == 0) v = 1;
    pkg.value = v;

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = constraints_literal_constraint_literal_constraint_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u value=%d bytes=%zu\n", seed, pkg.value, byte_pos);
    return 0;
}
