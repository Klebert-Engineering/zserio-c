/* Same shape as uint16/uint32_param_choice but with a uint64 selector. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/uint64_param_choice/u_int64_param_choice.h"

static uint64_t selector_for(unsigned seed)
{
    static const uint64_t table[] = {1u, 3u, 5u, 0xFFFFFFFFu, 17u};
    return table[seed % (sizeof(table) / sizeof(table[0]))];
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const uint64_t sel = selector_for(seed);

    choice_types_uint64_param_choice_u_int64_param_choice_t pkg;
    choice_types_uint64_param_choice_u_int64_param_choice_init(&pkg);
    choice_types_uint64_param_choice_u_int64_param_choice_set_parameters(&pkg, sel);

    if (sel == 1u) pkg.u.value_a = (int8_t)((int)seed - 50);
    else if (sel == 2u || sel == 3u || sel == 4u) pkg.u.value_b = (int16_t)(seed * 13u);
    else if (sel == 5u || sel == 6u) { /* empty */ }
    else pkg.u.value_c = (int32_t)((int)seed * 191);

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_uint64_param_choice_u_int64_param_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%lu bytes=%zu\n", seed, (unsigned long)sel, byte_pos);
    return 0;
}
