/* Multi-parameter struct nested in an array, with a dynamic-bit-width
 * field driven by a parameter:
 *
 *   struct Data13(varsize arrayLength) {
 *       int:13 array13[arrayLength];
 *   };
 *   struct TestStructure(varsize arrayLength, bit:4 bitFieldLength) {
 *       bit:5  array5[arrayLength];
 *       Data13(arrayLength) array13[arrayLength];
 *       int:17 field17;
 *       int<bitFieldLength> dynBitField;
 *   };
 *
 * Both parameters are passed via _set_parameters and not on the wire.
 * cpp_driver re-derives them from the seed. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/multiple_params/test_structure.h"
#include "parameterized_types/multiple_params/data13.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);

    const uint32_t arrayLength = (uint32_t)((seed % 3u) + 2u);  /* 2..4 */
    const uint8_t bitFieldLength = (uint8_t)((seed % 7u) + 2u);  /* 2..8 (fits in bit:4) */

    parameterized_types_multiple_params_test_structure_t pkg;
    parameterized_types_multiple_params_test_structure_init(&pkg);
    parameterized_types_multiple_params_test_structure_set_parameters(&pkg, arrayLength, bitFieldLength);

    /* array5: bit:5, range 0..31 */
    pkg.array5_count = arrayLength;
    pkg.array5 = (uint8_t *)calloc(arrayLength, sizeof(uint8_t));
    for (size_t i = 0; i < arrayLength; ++i)
        pkg.array5[i] = (uint8_t)((seed + (unsigned)i) & 0x1Fu);

    /* array13: array of Data13, each with its own int:13 array */
    pkg.array13_count = arrayLength;
    pkg.array13 = (parameterized_types_multiple_params_data13_t *)calloc(
            arrayLength, sizeof(*pkg.array13));
    for (size_t i = 0; i < arrayLength; ++i) {
        parameterized_types_multiple_params_data13_init(&pkg.array13[i]);
        parameterized_types_multiple_params_data13_set_parameters(&pkg.array13[i], arrayLength);
        pkg.array13[i].array13_count = arrayLength;
        pkg.array13[i].array13 = (int16_t *)calloc(arrayLength, sizeof(int16_t));
        for (size_t j = 0; j < arrayLength; ++j) {
            /* int:13 range: -4096..4095 */
            pkg.array13[i].array13[j] = (int16_t)((int)((seed + (unsigned)(i * 7 + j)) & 0x1FFFu) - 4096);
        }
    }

    /* int:17 range: -65536..65535 */
    pkg.field17 = (int32_t)((int)(seed & 0x1FFFFu) - 65536);
    /* int<bitFieldLength> range: -2^(N-1)..2^(N-1)-1 */
    const int64_t cap = (int64_t)1 << (bitFieldLength - 1u);
    pkg.dyn_bit_field = (int64_t)(seed % (uint32_t)(2 * cap)) - cap;

    uint8_t buf[1024];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_multiple_params_test_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u N=%u bitlen=%u bytes=%zu\n",
            seed, arrayLength, (unsigned)bitFieldLength, byte_pos);
    /* don't bother freeing; process is exiting */
    return 0;
}
