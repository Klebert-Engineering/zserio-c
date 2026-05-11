/* `numbits(value)` operator inside schema functions. The struct has
 * four numeric fields and four functions that compute their bit-
 * width. Functions don't affect wire bytes; just verify compile. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/numbits_operator/num_bits_functions.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_numbits_operator_num_bits_functions_t pkg;
    expressions_numbits_operator_num_bits_functions_init(&pkg);

    pkg.value8  = (uint8_t)(seed & 0xFFu);
    pkg.value16 = (uint16_t)(seed * 17u);
    pkg.value32 = (uint32_t)(seed * 1103u + 7u);
    pkg.value64 = (uint64_t)(seed * 2654435761ull) << 16;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_numbits_operator_num_bits_functions_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
