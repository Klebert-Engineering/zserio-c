/* Auto-optionals with and without default values. Defaults don't
 * affect the wire — each field has a 1-bit presence prefix and the
 * value follows iff present. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "default_values/optional_default_values/optional_default_values.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    default_values_optional_default_values_optional_default_values_t pkg;
    default_values_optional_default_values_optional_default_values_init(&pkg);

    /* Each of the 5 fields has its own seed bit for present-or-not. */
    if (seed & 0x01u) {
        pkg.optional_no_default_bool_field_present = true;
        pkg.optional_no_default_bool_field = (seed & 0x80u) != 0u;
    }
    if (seed & 0x02u) {
        pkg.optional_no_default_string_field_present = true;
        const char *s = "no-default";
        pkg.optional_no_default_string_field.bytes = (char *)s;
        pkg.optional_no_default_string_field.length = strlen(s);
    }
    if (seed & 0x04u) {
        pkg.optional_default_u32_field_present = true;
        pkg.optional_default_u32_field = 13u + (seed & 0xFFu);
    }
    if (seed & 0x08u) {
        pkg.optional_default_f64_field_present = true;
        pkg.optional_default_f64_field = 1.234 + (double)(seed % 10u);
    }
    if (seed & 0x10u) {
        pkg.optional_default_string_field_present = true;
        const char *s = "with-default";
        pkg.optional_default_string_field.bytes = (char *)s;
        pkg.optional_default_string_field.length = strlen(s);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = default_values_optional_default_values_optional_default_values_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
