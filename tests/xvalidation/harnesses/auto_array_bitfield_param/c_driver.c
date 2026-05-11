/* Parameterized struct with bit:4 numBits param + dynamic-width
 * auto-array of bit<numBits> elements. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/auto_array_bitfield_param/parameterized_bitfield_length.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_auto_array_bitfield_param_parameterized_bitfield_length_t pkg;
    array_types_auto_array_bitfield_param_parameterized_bitfield_length_init(&pkg);

    const uint8_t numBits = (uint8_t)((seed % 8u) + 1u);  /* 1..8 */
    array_types_auto_array_bitfield_param_parameterized_bitfield_length_set_parameters(&pkg, numBits);
    pkg.dynamic_bitfield_array_count = (size_t)((seed % 4u) + 2u);
    pkg.dynamic_bitfield_array =
            (uint64_t *)calloc(pkg.dynamic_bitfield_array_count, sizeof(uint64_t));
    const uint64_t mask = ((uint64_t)1 << numBits) - 1u;
    for (size_t i = 0; i < pkg.dynamic_bitfield_array_count; ++i)
        pkg.dynamic_bitfield_array[i] = (uint64_t)((seed + (unsigned)i) & mask);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_auto_array_bitfield_param_parameterized_bitfield_length_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u nb=%u bytes=%zu\n", seed, (unsigned)numBits, byte_pos);
    return 0;
}
