/* Constraints in the schema force specific values:
 *   blackColor must be BLACK
 *   whiteColor (when present) must be WHITE
 *   purpleColor (when hasPurple) must be PURPLE
 * The seed varies only the optional/conditional presence, never the
 * field values themselves (which are pinned by the constraints). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "constraints/structure_constraints/structure_constraints.h"
#include "constraints/structure_constraints/basic_color.h"
#include "constraints/structure_constraints/extended_color.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    constraints_structure_constraints_structure_constraints_t pkg;
    constraints_structure_constraints_structure_constraints_init(&pkg);

    pkg.black_color = constraints_structure_constraints_BASIC_COLOR_BLACK;
    pkg.white_color_present = (seed & 1u) != 0u;
    if (pkg.white_color_present) {
        pkg.white_color = constraints_structure_constraints_BASIC_COLOR_WHITE;
    }
    pkg.has_purple = (seed & 2u) != 0u;
    if (pkg.has_purple) {
        pkg.purple_color_present = true;
        pkg.purple_color = constraints_structure_constraints_EXTENDED_COLOR_PURPLE;
    }

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = constraints_structure_constraints_structure_constraints_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u w?%d p?%d bytes=%zu\n", seed,
            pkg.white_color_present, pkg.has_purple, byte_pos);
    return 0;
}
