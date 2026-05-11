/* Choice with per-arm constraints:
 *   case true:  uint8 value8 : value8 != 0;
 *   case false: uint16 value16 : value16 > 255;
 * Driver picks selector and a constraint-satisfying value. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "constraints/choice_constraints/choice_constraints.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const bool sel = (seed & 1u) != 0u;

    constraints_choice_constraints_choice_constraints_t pkg;
    constraints_choice_constraints_choice_constraints_init(&pkg);
    constraints_choice_constraints_choice_constraints_set_parameters(&pkg, sel);
    if (sel) {
        pkg.u.value8 = (uint8_t)((seed % 255u) + 1u);  /* non-zero */
    } else {
        pkg.u.value16 = (uint16_t)(256u + (seed & 0x3FFFu));  /* > 255 */
    }

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = constraints_choice_constraints_choice_constraints_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%d bytes=%zu\n", seed, (int)sel, byte_pos);
    return 0;
}
