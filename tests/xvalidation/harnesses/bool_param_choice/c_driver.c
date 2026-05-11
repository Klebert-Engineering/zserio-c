#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/bool_param_choice/bool_param_choice.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    choice_types_bool_param_choice_bool_param_choice_t pkg;
    choice_types_bool_param_choice_bool_param_choice_init(&pkg);
    const bool selector = (seed & 1u) != 0u;
    choice_types_bool_param_choice_bool_param_choice_set_parameters(&pkg, selector);
    uint32_t s = seed * 2654435761u + 1u;
    s = s * 1103515245u + 12345u;
    if (selector) {
        pkg.u.black = (int8_t)((s >> 16) & 0x7fu);
    } else {
        pkg.u.grey = (int16_t)((s >> 8) & 0x7fffu);
    }

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_bool_param_choice_bool_param_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u selector=%d bytes=%zu\n", seed, selector, byte_pos);
    return 0;
}
