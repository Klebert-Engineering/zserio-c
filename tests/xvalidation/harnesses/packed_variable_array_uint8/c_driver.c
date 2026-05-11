/*
 * C driver for the schema-driven cross-validation harness.
 *
 *   c_driver <seed> <out.bin>
 *
 * Fills an `array_types.packed_variable_array_uint8.PackedVariableArray`
 * with deterministic data derived from <seed>, encodes via the
 * generated C writer, and writes the resulting bytes to <out.bin>.
 *
 * The cpp side then reads the bytes, decodes, re-encodes, and compares
 * its re-encoded bytes against <out.bin>. Both bytes equal ⇒ this
 * schema + seed round-trips byte-exact across both wire-format
 * implementations.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "array_types/packed_variable_array_uint8/packed_variable_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]);
        return 2;
    }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const char *out_path = argv[2];

    /* Deterministic fill: count in [1, 32], values in [0, 255]
     * derived from a tiny LCG seeded by the harness seed. */
    const size_t count = (size_t)((seed % 32U) + 1U);
    uint8_t values[64];
    {
        uint32_t s = seed * 2654435761u + 1u;
        for (size_t i = 0; i < count; ++i) {
            s = s * 1103515245u + 12345u;
            values[i] = (uint8_t)((s >> 16) & 0xffu);
        }
    }

    array_types_packed_variable_array_uint8_packed_variable_array_t pkg;
    array_types_packed_variable_array_uint8_packed_variable_array_init(&pkg);
    pkg.num_elements = (uint32_t)count;
    pkg.uint8_array_count = count;
    pkg.uint8_array = malloc(count * sizeof(uint8_t));
    if (!pkg.uint8_array) { perror("malloc"); return 1; }
    memcpy(pkg.uint8_array, values, count);

    /* Encode. A small fixed buffer is enough — count<=32 packed uint8
     * fits in well under 100 bytes. */
    uint8_t buf[512];
    zserio_bitstream_writer_t w;
    zserio_status_t st = zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    if (st != ZSERIO_OK) {
        fprintf(stderr, "writer_init: %d\n", (int)st);
        return 1;
    }
    st = array_types_packed_variable_array_uint8_packed_variable_array_write(&pkg, &w);
    if (st != ZSERIO_OK) {
        fprintf(stderr, "write: %d\n", (int)st);
        return 1;
    }
    const size_t bit_pos = zserio_bitstream_writer_position(&w);
    const size_t byte_pos = (bit_pos + 7u) / 8u;

    FILE *f = fopen(out_path, "wb");
    if (!f) { perror("fopen"); return 1; }
    if (fwrite(buf, 1, byte_pos, f) != byte_pos) {
        perror("fwrite");
        fclose(f);
        return 1;
    }
    fclose(f);
    fprintf(stdout, "c_driver: seed=%u count=%zu bytes=%zu\n",
            seed, count, byte_pos);

    free(pkg.uint8_array);
    return 0;
}
