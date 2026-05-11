/* Root choice with enum selector and fully-qualified case labels. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/full_enum_param_choice/full_enum_param_choice.h"
#include "choice_types/full_enum_param_choice/selector.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const choice_types_full_enum_param_choice_selector_t sel =
        (choice_types_full_enum_param_choice_selector_t)(seed % 3u);

    choice_types_full_enum_param_choice_full_enum_param_choice_t pkg;
    choice_types_full_enum_param_choice_full_enum_param_choice_init(&pkg);
    choice_types_full_enum_param_choice_full_enum_param_choice_set_parameters(&pkg, sel);
    switch ((int)sel) {
        case 0: pkg.u.black = (int8_t)(seed & 0x7Fu); break;
        case 1: pkg.u.grey  = (int16_t)(seed * 7u); break;
        case 2: pkg.u.white = (int32_t)(seed * 991u); break;
        default: break;
    }

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_full_enum_param_choice_full_enum_param_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%u bytes=%zu\n", seed, (unsigned)sel, byte_pos);
    return 0;
}
