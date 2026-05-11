/* Dynamic bit-field widths driven by schema function values:
 *   bit<length3()>  bitField3;  // width = lengthof(array) = 7 (fixed-len array)
 *   bit<length4()>  bitField4;  // width = id & 0x0F
 *   bit<length5()>  bitField5;  // width = id % 32
 * Exercises the fixed-length `lengthof` fix (task #82). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "builtin_types/bitfield_function_length/container.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    builtin_types_bitfield_function_length_container_t pkg;
    builtin_types_bitfield_function_length_container_init(&pkg);

    /* id picked so id & 0x0F is in 1..15 and id % 32 is in 1..31. */
    pkg.id = (uint64_t)((seed & 0x0Eu) | 0x01u);
    if ((pkg.id % 32u) == 0u) pkg.id |= 0x10u;
    for (size_t i = 0; i < 7; ++i)
        pkg.array[i] = (uint64_t)(seed + (unsigned)i);

    /* bit:7 (lengthof returns 7) — range 0..127 */
    pkg.bit_field3 = (uint64_t)((seed + 1u) & 0x7Fu);
    /* bit:(id & 0xF) — keep value small to fit */
    pkg.bit_field4 = (uint64_t)((seed >> 1) & 0x01u);
    /* bit:(id % 32) — same */
    pkg.bit_field5 = (uint64_t)((seed >> 2) & 0x01u);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = builtin_types_bitfield_function_length_container_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
