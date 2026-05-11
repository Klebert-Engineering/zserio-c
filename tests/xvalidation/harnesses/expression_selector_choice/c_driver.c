/* Choice with an expression-based selector (`on tag + 1`). The
 * parameter `tag` is uint16; the selector at the case-match site is
 * `tag + 1`. Cases: 1, 2, default. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/expression_selector_choice/expression_selector_choice.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    /* tag values 0,1,2 → selector 1,2,3 → case 1, case 2, default. */
    const uint16_t tag = (uint16_t)(seed % 3u);

    choice_types_expression_selector_choice_expression_selector_choice_t pkg;
    choice_types_expression_selector_choice_expression_selector_choice_init(&pkg);
    choice_types_expression_selector_choice_expression_selector_choice_set_parameters(&pkg, tag);
    if (tag == 0u) pkg.u.field8  = (int8_t)((int)((seed * 5u) & 0x7Fu) - 32);
    else if (tag == 1u) pkg.u.field16 = (int16_t)((int)(seed * 17u) - 4096);
    else pkg.u.field32 = (int32_t)(seed * 2654435761u);

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_expression_selector_choice_expression_selector_choice_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u tag=%u bytes=%zu\n", seed, (unsigned)tag, byte_pos);
    return 0;
}
