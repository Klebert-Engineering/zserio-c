/* Choice with two case arms + an EMPTY `default:` branch. Wire
 * format: just the active arm's bytes (default writes nothing). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/default_empty_choice/default_empty_choice.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    /* tag = 1 / 2 / 3 (default) — bit:3, so tag in 0..7; we pick 1,2,3. */
    const uint8_t tag = (uint8_t)((seed % 3u) + 1u);
    choice_types_default_empty_choice_default_empty_choice_t pkg;
    choice_types_default_empty_choice_default_empty_choice_init(&pkg);
    choice_types_default_empty_choice_default_empty_choice_set_parameters(&pkg, tag);
    if (tag == 1u) pkg.u.value_a = (int8_t)((int)(seed & 0x7Fu) - 64);
    else if (tag == 2u) pkg.u.value_b = (int16_t)((int)seed - 1024);
    /* tag == 3u → default arm, no fields */

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_default_empty_choice_default_empty_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u tag=%u bytes=%zu\n", seed, (unsigned)tag, byte_pos);
    return 0;
}
