/* Structs named Byte/Short/Integer/Long/BigInteger/Float/Double/String
 * (Java wrapper-class names). Generator must not collide with built-in
 * primitives in any target. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "identifiers/structure_name_clashing_with_java/structure_name_clashing_with_java.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    identifiers_structure_name_clashing_with_java_structure_name_clashing_with_java_t pkg;
    identifiers_structure_name_clashing_with_java_structure_name_clashing_with_java_init(&pkg);

    /* Set each optional value field present and write a seeded value. */
    pkg.byte_field.value = (int8_t)((int)(seed & 0x7Fu) - 30);          pkg.byte_field.value_present = true;
    pkg.short_field.value = (int16_t)(seed * 7u);                       pkg.short_field.value_present = true;
    pkg.integer_field.value = (int32_t)(seed * 13u);                    pkg.integer_field.value_present = true;
    pkg.long_field.value = (int64_t)((int64_t)seed * 991);              pkg.long_field.value_present = true;
    pkg.big_integer_field.value = (uint64_t)seed * 1103u;               pkg.big_integer_field.value_present = true;
    pkg.float_field.value = (float)(seed * 0.5f);                       pkg.float_field.value_present = true;
    pkg.double_field.value = (double)(seed * 0.125);                    pkg.double_field.value_present = true;

    static char text[32]; int n = snprintf(text, sizeof(text), "s-%u", seed);
    pkg.string_field.value.bytes = text;
    pkg.string_field.value.length = (size_t)n;
    pkg.string_field.value_present = true;

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = identifiers_structure_name_clashing_with_java_structure_name_clashing_with_java_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
