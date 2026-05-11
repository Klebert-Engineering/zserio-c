/* Choice with a `case <CONST>:` label (UINT8_CONST = 13). Selector
 * is a uint8 parameter, not on the wire. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "choice_types/constant_in_choice_case/constant_in_choice_case.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    /* selector=0 → zeroCase (uint32); selector=13 → constCase (uint8). */
    const uint8_t selector = (seed & 1u) ? 13u : 0u;

    choice_types_constant_in_choice_case_constant_in_choice_case_t pkg;
    choice_types_constant_in_choice_case_constant_in_choice_case_init(&pkg);
    choice_types_constant_in_choice_case_constant_in_choice_case_set_parameters(&pkg, selector);
    if (selector == 0u) pkg.u.zero_case  = 0xDEADBEEFu ^ seed;
    else                pkg.u.const_case = (uint8_t)(seed & 0xFFu);

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = choice_types_constant_in_choice_case_constant_in_choice_case_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%u bytes=%zu\n", seed, (unsigned)selector, byte_pos);
    return 0;
}
