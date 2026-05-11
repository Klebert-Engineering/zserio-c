/* Choice with only a `default:` branch (no explicit cases). Boolean
 * selector parameter; both true and false select the default arm.
 * Selector is not on the wire — orchestrator-supplied seed. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/bool_choice_with_default/bool_choice_with_default.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const bool sel = (seed & 1u) != 0u;

    choice_types_bool_choice_with_default_bool_choice_with_default_t pkg;
    choice_types_bool_choice_with_default_bool_choice_with_default_init(&pkg);
    choice_types_bool_choice_with_default_bool_choice_with_default_set_parameters(&pkg, sel);
    pkg.u.field = (uint8_t)(seed & 0xFFu);

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_bool_choice_with_default_bool_choice_with_default_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%d bytes=%zu\n", seed, (int)sel, byte_pos);
    return 0;
}
