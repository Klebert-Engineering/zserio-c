/* Three dynamic-bit fields with widths driven by different upstream
 * field types (bit:4, uint8, uint64). Verifies all three width-storage
 * mappings work. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "builtin_types/dynamic_bitfield_length_bounds/container.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    builtin_types_dynamic_bitfield_length_bounds_container_t pkg;
    builtin_types_dynamic_bitfield_length_bounds_container_init(&pkg);

    /* bit:4 width — 1..15 */
    pkg.unsigned_bit_length = (uint8_t)((seed % 15u) + 1u);
    pkg.unsigned_value = (uint64_t)((seed * 17u) & ((1ull << pkg.unsigned_bit_length) - 1u));

    /* uint8 width — 1..64 */
    pkg.unsigned_big_bit_length = (uint8_t)((seed % 64u) + 1u);
    pkg.unsigned_big_value = pkg.unsigned_big_bit_length == 64u
            ? (uint64_t)seed * 0x0101010101010101ull
            : (uint64_t)((seed * 1103u) & ((1ull << pkg.unsigned_big_bit_length) - 1u));

    /* uint64 width — 1..64. The signed range for int<N> is
     * [-2^(N-1), 2^(N-1) - 1]; pick a small in-range value derived
     * from the seed and avoid 64-bit shift / modulo overflow for
     * the wide widths (signed_bit_length == 64 makes the cap shift
     * undefined; signed_bit_length >= 33 overflows a uint32-domain
     * modulo, which on CI surfaces as SIGFPE — locally the
     * compiler may fold differently). Cap the magnitude at the
     * width or 16, whichever is smaller, so the value always fits. */
    pkg.signed_bit_length = (uint64_t)((seed % 64u) + 1u);
    if (pkg.signed_bit_length == 1u) {
        pkg.signed_value = (int64_t)(-((int)(seed & 1u)));  /* -1..0 */
    } else {
        const uint32_t span = (pkg.signed_bit_length >= 5u) ? 16u
                : (uint32_t)(1ull << (pkg.signed_bit_length - 1u));
        const int32_t half = (int32_t)(span / 2u);
        pkg.signed_value = (int64_t)((int32_t)(seed % span) - half);
    }

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = builtin_types_dynamic_bitfield_length_bounds_container_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
