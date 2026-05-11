/* Root choice with uint16 selector covering case-fall-through (2/3/4),
 * empty cases (5/6), and default. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/uint16_param_choice/u_int16_param_choice.h"

static uint16_t selector_for(unsigned seed)
{
    static const uint16_t table[] = {1u, 3u, 5u, 99u, 17u};
    return table[seed % (sizeof(table) / sizeof(table[0]))];
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const uint16_t sel = selector_for(seed);

    choice_types_uint16_param_choice_u_int16_param_choice_t pkg;
    choice_types_uint16_param_choice_u_int16_param_choice_init(&pkg);
    choice_types_uint16_param_choice_u_int16_param_choice_set_parameters(&pkg, sel);

    if (sel == 1u) pkg.u.value_a = (int8_t)((int)seed - 50);
    else if (sel == 2u || sel == 3u || sel == 4u) pkg.u.value_b = (int16_t)(seed * 13u);
    else if (sel == 5u || sel == 6u) { /* empty arms — nothing to set */ }
    else pkg.u.value_c = (int32_t)((int)seed * 191);

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_uint16_param_choice_u_int16_param_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%u bytes=%zu\n", seed, (unsigned)sel, byte_pos);
    return 0;
}
