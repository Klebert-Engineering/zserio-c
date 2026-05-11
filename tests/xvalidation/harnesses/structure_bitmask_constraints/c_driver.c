/* Constraint expressions involving bitmask `&` and `==` against
 * sibling fields. coordX must equal 0 unless `availability & COORD_X`
 * is set; same for Y and Z. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "constraints/structure_bitmask_constraints/structure_bitmask_constraints.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    constraints_structure_bitmask_constraints_structure_bitmask_constraints_t pkg;
    constraints_structure_bitmask_constraints_structure_bitmask_constraints_init(&pkg);

    /* Cycle through {0b001, 0b010, 0b100, 0b111, 0b011} for full coverage. */
    static const uint8_t bits[] = {0x1u, 0x2u, 0x4u, 0x7u, 0x3u};
    pkg.availability =
        (constraints_structure_bitmask_constraints_availability_t)bits[seed % 5u];
    pkg.coord_x = (pkg.availability & 0x1u) ? (uint8_t)((seed * 7u) & 0xFFu) : 0u;
    pkg.coord_y = (pkg.availability & 0x2u) ? (uint8_t)((seed * 11u) & 0xFFu) : 0u;
    pkg.coord_z = (pkg.availability & 0x4u) ? (uint8_t)((seed * 13u) & 0xFFu) : 0u;

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = constraints_structure_bitmask_constraints_structure_bitmask_constraints_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
