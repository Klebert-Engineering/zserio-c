/* Ternary expressions in array length AND dynamic-bit-field width:
 *
 *   VariableArray { bool isFirstSizeUsed; bit:6 firstSize=10; bit:6
 *                   secondSize=20;
 *                   VariableArrayElement(isFirstSizeUsed?firstSize:secondSize)
 *                     array[isFirstSizeUsed ? firstSize*firstSize
 *                                            : secondSize*secondSize];}
 *
 * Per-element width is min(bitSize, 32) bits. Driver sets all sizes
 * to fixed values and fills array elements within range. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/variable_array_ternary_operator/variable_array.h"
#include "array_types/variable_array_ternary_operator/variable_array_element.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_variable_array_ternary_operator_variable_array_t pkg;
    array_types_variable_array_ternary_operator_variable_array_init(&pkg);

    /* Use the "first" arm: array length = firstSize * firstSize; element
     * bit width = firstSize (since firstSize=10 < 32). */
    pkg.is_first_size_used = true;
    pkg.first_size = 10u;
    pkg.second_size = 20u;
    const size_t count = (size_t)pkg.first_size * (size_t)pkg.first_size;  /* 100 */

    pkg.array_count = count;
    pkg.array = (array_types_variable_array_ternary_operator_variable_array_element_t *)calloc(
            count, sizeof(*pkg.array));
    for (size_t i = 0; i < count; ++i) {
        array_types_variable_array_ternary_operator_variable_array_element_init(&pkg.array[i]);
        array_types_variable_array_ternary_operator_variable_array_element_set_parameters(
                &pkg.array[i], pkg.first_size);
        pkg.array[i].max_bit_size = 32u;  /* schema default */
        /* element is bit:10 here, range 0..1023 */
        pkg.array[i].element = (uint64_t)((seed + (unsigned)i) & 0x3FFu);
    }

    uint8_t buf[1024];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_variable_array_ternary_operator_variable_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u count=%zu bytes=%zu\n", seed, count, byte_pos);
    return 0;
}
