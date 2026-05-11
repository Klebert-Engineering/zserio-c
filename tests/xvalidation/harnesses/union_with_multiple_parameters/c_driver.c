/* Union with two parameters (varsize arrayLength + bit:4 bitFieldLength).
 * Both are parameters (not on the wire). Driver derives them from seed
 * and cpp_driver does the same. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "union_types/union_with_multiple_parameters/test_union.h"
#include "union_types/union_with_multiple_parameters/data13.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const uint32_t arrayLength = (uint32_t)((seed % 3u) + 2u);
    const uint8_t  bitFieldLength = (uint8_t)((seed % 7u) + 2u);
    const unsigned arm = seed % 4u;

    union_types_union_with_multiple_parameters_test_union_t pkg;
    union_types_union_with_multiple_parameters_test_union_init(&pkg);
    union_types_union_with_multiple_parameters_test_union_set_parameters(&pkg, arrayLength, bitFieldLength);
    pkg.tag = arm;
    switch (arm) {
        case 0u: {  /* bit:5 array5[arrayLength] */
            pkg.array5_count = arrayLength;
            pkg.u.array5 = (uint8_t *)calloc(arrayLength, sizeof(uint8_t));
            for (uint32_t i = 0; i < arrayLength; ++i)
                pkg.u.array5[i] = (uint8_t)((seed + i) & 0x1Fu);
            break;
        }
        case 1u: {  /* Data13(arrayLength) array13[arrayLength] */
            pkg.array13_count = arrayLength;
            pkg.u.array13 = (union_types_union_with_multiple_parameters_data13_t *)calloc(
                    arrayLength, sizeof(*pkg.u.array13));
            for (uint32_t i = 0; i < arrayLength; ++i) {
                union_types_union_with_multiple_parameters_data13_init(&pkg.u.array13[i]);
                union_types_union_with_multiple_parameters_data13_set_parameters(&pkg.u.array13[i], arrayLength);
                pkg.u.array13[i].array13_count = arrayLength;
                pkg.u.array13[i].array13 = (int16_t *)calloc(arrayLength, sizeof(int16_t));
                for (uint32_t j = 0; j < arrayLength; ++j)
                    pkg.u.array13[i].array13[j] = (int16_t)((int)((seed + i + j) & 0x1FFFu) - 4096);
            }
            break;
        }
        case 2u:    /* int:17 field17 */
            pkg.u.field17 = (int32_t)((int)(seed & 0x1FFFFu) - 65536);
            break;
        case 3u: {  /* int<bitFieldLength> dynBitField */
            const int64_t cap = (int64_t)1 << (bitFieldLength - 1u);
            pkg.u.dyn_bit_field = (int64_t)(seed % (uint32_t)(2 * cap)) - cap;
            break;
        }
        default: break;
    }

    uint8_t buf[512];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = union_types_union_with_multiple_parameters_test_union_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u N=%u bl=%u arm=%u bytes=%zu\n",
            seed, arrayLength, bitFieldLength, arm, byte_pos);
    return 0;
}
