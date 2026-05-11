/* Array constraint via `lengthof`:
 *   uint32 array[] : lengthof(array) > 5 && lengthof(array) < 10;
 * Driver picks a length in (5, 10) — i.e. 6..9 — and fills it. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "constraints/array_lengthof_constraint/array_lengthof_constraint.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    constraints_array_lengthof_constraint_array_lengthof_constraint_t pkg;
    constraints_array_lengthof_constraint_array_lengthof_constraint_init(&pkg);

    pkg.array_count = (size_t)(6u + (seed % 4u));  /* 6..9 — passes constraint */
    pkg.array = (uint32_t *)calloc(pkg.array_count, sizeof(uint32_t));
    for (size_t i = 0; i < pkg.array_count; ++i)
        pkg.array[i] = (uint32_t)(seed * 1103u + (unsigned)i);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = constraints_array_lengthof_constraint_array_lengthof_constraint_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.array); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u count=%zu bytes=%zu\n", seed, pkg.array_count, byte_pos);
    free(pkg.array);
    return 0;
}
