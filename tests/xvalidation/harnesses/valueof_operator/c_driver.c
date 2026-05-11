/* `valueof(enumValue)` — extract the underlying integer of an enum.
 * Functions only; wire bytes are just the Color enum (uint8). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/valueof_operator/value_of_functions.h"
#include "expressions/valueof_operator/color.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_valueof_operator_value_of_functions_t pkg;
    expressions_valueof_operator_value_of_functions_init(&pkg);

    /* Color: WHITE=1, BLACK=2. Pick by parity. */
    pkg.color = (seed & 1u) ? 1u : 2u;

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_valueof_operator_value_of_functions_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
