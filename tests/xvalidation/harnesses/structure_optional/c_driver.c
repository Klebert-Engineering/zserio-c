/* ValueCalculator has a bit:4 default + bit:4 optional. The function
 * value() returns one or the other. ValueConsumerCreator chains a
 * ValueConsumer parameterized by the function's return value. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/structure_optional/value_consumer_creator.h"
#include "functions/structure_optional/value_calculator.h"
#include "functions/structure_optional/value_consumer.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_structure_optional_value_consumer_creator_t pkg;
    functions_structure_optional_value_consumer_creator_init(&pkg);

    /* Pick whether to use the external value or the default value. */
    if (seed & 1u) {
        pkg.value_calculator.default_value = 0u;
        pkg.value_calculator.external_value_present = true;
        pkg.value_calculator.external_value = (uint8_t)(((seed >> 1) & 0x07u));
        functions_structure_optional_value_consumer_set_parameters(
                &pkg.value_consumer, pkg.value_calculator.external_value);
    } else {
        pkg.value_calculator.default_value = (uint8_t)(((seed >> 1) | 1u) & 0x0Fu);
        functions_structure_optional_value_consumer_set_parameters(
                &pkg.value_consumer, pkg.value_calculator.default_value);
    }
    /* isSmall constraint: value < 8 → set isSmall appropriately. */
    pkg.value_consumer.is_small = (pkg.value_consumer.value < 8u);

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_structure_optional_value_consumer_creator_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
