/* `bit<length>` with length: uint64. Both unsigned and signed
 * variants. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "builtin_types/bitfield_uint64_length/container.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    builtin_types_bitfield_uint64_length_container_t pkg;
    builtin_types_bitfield_uint64_length_container_init(&pkg);

    pkg.length = (uint64_t)(((seed % 32u) + 1u));  /* 1..32 */
    pkg.unsigned_bit_field = (uint64_t)((seed * 17u) & ((1ull << pkg.length) - 1u));
    const int64_t halfRange = (int64_t)1 << (pkg.length - 1u);
    pkg.signed_bit_field   = (int64_t)((int)(seed % (uint32_t)halfRange) - halfRange / 2);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = builtin_types_bitfield_uint64_length_container_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
