/* Choice whose case arms each carry a variable-length array. Selector
 * is an int8 parameter, NOT on the wire — orchestrator passes seed,
 * driver derives selector. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/choice_with_array/test_choice.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const int8_t selector = (seed & 1u) ? (int8_t)8 : (int8_t)16;

    choice_types_choice_with_array_test_choice_t pkg;
    choice_types_choice_with_array_test_choice_init(&pkg);
    choice_types_choice_with_array_test_choice_set_parameters(&pkg, selector);

    if (selector == 8) {
        pkg.array8_count = (size_t)((seed % 4u) + 1u);
        pkg.u.array8 = (choice_types_choice_with_array_data8_t *)calloc(
                pkg.array8_count, sizeof(*pkg.u.array8));
        for (size_t i = 0; i < pkg.array8_count; ++i)
            pkg.u.array8[i].data = (int8_t)((int)((seed + (unsigned)i) & 0x7Fu));
    } else {
        pkg.array16_count = (size_t)((seed % 3u) + 2u);
        pkg.u.array16 = (int16_t *)calloc(pkg.array16_count, sizeof(int16_t));
        for (size_t i = 0; i < pkg.array16_count; ++i)
            pkg.u.array16[i] = (int16_t)((int)(seed * 17u + (unsigned)i) - 128);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_choice_with_array_test_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%d bytes=%zu\n", seed, (int)selector, byte_pos);
    return 0;
}
