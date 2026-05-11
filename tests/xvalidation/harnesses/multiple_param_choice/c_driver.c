/* Choice with three parameters: selector, arrayLength, bitFieldLength.
 * Covers case 5 (bit:5 array), 13 (compound array via Data13), 17 (single
 * int:17 field), and default (dynamic-bit int<bitFieldLength>). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/multiple_param_choice/test_choice.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    static const uint8_t sel_table[] = {5u, 13u, 17u, 99u, 1u};
    static const size_t  len_table[] = {3u,  2u,  0u, 2u,  4u};
    const uint8_t sel = sel_table[seed % 5u];
    const size_t  len = len_table[seed % 5u];
    const uint8_t bit_field_length = (uint8_t)((seed % 5u) + 3u); /* 3..7 bits */

    choice_types_multiple_param_choice_test_choice_t pkg;
    choice_types_multiple_param_choice_test_choice_init(&pkg);
    choice_types_multiple_param_choice_test_choice_set_parameters(&pkg, sel, (uint32_t)len, bit_field_length);

    switch ((int)sel) {
        case 5: {
            pkg.array5_count = len;
            pkg.u.array5 = (uint8_t *)calloc(len ? len : 1, sizeof(uint8_t));
            for (size_t i = 0; i < len; ++i) pkg.u.array5[i] = (uint8_t)((seed + (unsigned)i) & 0x1Fu);
            break;
        }
        case 13: {
            pkg.array13_count = len;
            pkg.u.array13 = (choice_types_multiple_param_choice_data13_t *)calloc(len ? len : 1,
                    sizeof(*pkg.u.array13));
            for (size_t i = 0; i < len; ++i) {
                choice_types_multiple_param_choice_data13_init(&pkg.u.array13[i]);
                choice_types_multiple_param_choice_data13_set_parameters(&pkg.u.array13[i], (uint32_t)len);
                pkg.u.array13[i].array13_count = len;
                pkg.u.array13[i].array13 = (int16_t *)calloc(len, sizeof(int16_t));
                for (size_t j = 0; j < len; ++j)
                    pkg.u.array13[i].array13[j] = (int16_t)((int)(seed + (unsigned)(i * 10 + j)) - 1000);
            }
            break;
        }
        case 17:
            pkg.u.field17 = (int32_t)((int)seed - 60000);
            break;
        default:
            pkg.u.dyn_bit_field = (int32_t)((int)((seed & 0xFu)) - 4);
            break;
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_multiple_param_choice_test_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%u len=%zu bfl=%u bytes=%zu\n",
            seed, (unsigned)sel, len, (unsigned)bit_field_length, byte_pos);
    return 0;
}
