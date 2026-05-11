/* Choice selector is a function-call on a parameter struct. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/function_selector_choice/test_choice.h"
#include "choice_types/function_selector_choice/selector.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);

    choice_types_function_selector_choice_selector_t sel;
    sel.num_bits = (seed & 1u) ? (int8_t)8 : (int8_t)16;

    choice_types_function_selector_choice_test_choice_t pkg;
    choice_types_function_selector_choice_test_choice_init(&pkg);
    choice_types_function_selector_choice_test_choice_set_parameters(&pkg, sel);
    if (sel.num_bits == 8)
        pkg.u.field8 = (int8_t)((int)seed - 50);
    else
        pkg.u.field16 = (int16_t)(seed * 7u);

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_function_selector_choice_test_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u num_bits=%d bytes=%zu\n", seed, (int)sel.num_bits, byte_pos);
    return 0;
}
