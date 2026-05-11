/* lengthof on every shape: fixed array, variable array, string
 * field, bytes field, string-array element, bytes-array element,
 * string literal, top-level string constant. Schema constrains
 * strField.length > lengthof(STR_CONSTANT)=11, and bytesField.length
 * < strField.length. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "expressions/lengthof_operator/length_of_functions.h"

static const char *kStrings[] = { "abcdefghijklmno", "one two three four" };  /* both > 11 bytes */
static const uint8_t kBytesA[] = { 0xAA, 0xBB, 0xCC };

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    expressions_lengthof_operator_length_of_functions_t pkg;
    expressions_lengthof_operator_length_of_functions_init(&pkg);

    for (size_t i = 0; i < 10; ++i)
        pkg.fixed_array[i] = (uint8_t)((seed + (unsigned)i) & 0xFFu);

    pkg.num_elements = (uint8_t)((seed % 4u) + 1u);
    pkg.variable_array_count = pkg.num_elements;
    pkg.variable_array = (uint8_t *)calloc(pkg.variable_array_count, sizeof(uint8_t));
    for (size_t i = 0; i < pkg.variable_array_count; ++i)
        pkg.variable_array[i] = (uint8_t)(seed * 7u + (unsigned)i);

    /* literalLengthField default = lengthof("€literal") = 10 in our
     * codegen, but the value isn't constrained by anything — we just
     * set it explicitly so writer + cpp roundtrip the same value. */
    pkg.literal_length_field = 10u;

    const char *s = kStrings[seed % 2u];
    pkg.str_field.bytes = (char *)s;
    pkg.str_field.length = strlen(s);  /* > 11 ⇒ passes lengthof(strField) > lengthof(STR_CONSTANT) */

    /* bytesField.length < strField.length */
    pkg.bytes_field.bytes = (uint8_t *)kBytesA;
    pkg.bytes_field.length = sizeof(kBytesA);

    pkg.str_array_count = 2;
    pkg.str_array = (zserio_string_t *)calloc(2, sizeof(zserio_string_t));
    pkg.str_array[0].bytes = (char *)"alpha";   pkg.str_array[0].length = 5;
    pkg.str_array[1].bytes = (char *)"beta";    pkg.str_array[1].length = 4;

    pkg.bytes_array_count = 1;
    pkg.bytes_array = (zserio_bytes_t *)calloc(1, sizeof(zserio_bytes_t));
    pkg.bytes_array[0].bytes = (uint8_t *)kBytesA;
    pkg.bytes_array[0].length = 2;

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = expressions_lengthof_operator_length_of_functions_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
