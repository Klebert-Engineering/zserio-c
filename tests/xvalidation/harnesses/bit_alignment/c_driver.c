/* Schema exercises every `align(N):` form: align to 1..8 (sub-byte),
 * 16, 32, 64, with `alignment_NN_break` fields just before each
 * larger boundary to ensure the alignment skip happens. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "alignment/bit_alignment/bit_alignment.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    alignment_bit_alignment_bit_alignment_t pkg;
    alignment_bit_alignment_bit_alignment_init(&pkg);

    /* Each field is a bit:N; pick values that fit. seed-derived but capped. */
    pkg.aligned1_field      = (uint8_t)((seed >> 0)  & 0x01u);
    pkg.aligned2_field      = (uint8_t)((seed >> 1)  & 0x03u);
    pkg.aligned3_field      = (uint8_t)((seed >> 2)  & 0x07u);
    pkg.aligned4_field      = (uint8_t)((seed >> 3)  & 0x0Fu);
    pkg.aligned5_field      = (uint8_t)((seed >> 4)  & 0x1Fu);
    pkg.aligned6_field      = (uint8_t)((seed >> 5)  & 0x3Fu);
    pkg.aligned7_field      = (uint8_t)((seed >> 6)  & 0x7Fu);
    pkg.aligned8_field      = (uint8_t)((seed >> 7)  & 0xFFu);
    pkg.alignment16_break   = (uint8_t)(seed & 0x01u);
    pkg.aligned16_field     = (uint16_t)(0xBEEFu ^ (uint16_t)seed);
    pkg.alignment32_break   = (uint8_t)((seed >> 8) & 0x01u);
    pkg.aligned32_field     = 0xDEADBEEFu ^ seed;
    pkg.alignment64_break   = (uint64_t)((seed >> 9) & 0x1ull);  /* bit:33 */
    pkg.aligned64_field     = (uint64_t)(0x0123456789ABCDEFull ^ ((uint64_t)seed << 16));

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = alignment_bit_alignment_bit_alignment_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
