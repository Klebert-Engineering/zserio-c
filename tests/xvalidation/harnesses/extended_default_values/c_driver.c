/* `extend` field stack with mixed defaults: bool/string with literal
 * defaults, optional float/string with defaults, plus extend uint32 /
 * extern / optional uint32 / optional bytes without defaults. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include <zserio/bytes.h>
#include <zserio/extern.h>
#include "default_values/extended_default_values/extended_default_values.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    default_values_extended_default_values_extended_default_values_t pkg;
    default_values_extended_default_values_extended_default_values_init(&pkg);

    pkg.no_default_u32_field = (uint32_t)(seed * 191u);
    static char no_def_str[32];
    int n1 = snprintf(no_def_str, sizeof(no_def_str), "nd-%u", seed);
    pkg.no_default_string_field.bytes = no_def_str;
    pkg.no_default_string_field.length = (size_t)n1;

    pkg.extended_default_bool_field = (seed & 1u) ? true : false;
    static char def_str[32];
    int n2 = snprintf(def_str, sizeof(def_str), "ed-%u", seed);
    pkg.extended_default_string_field.bytes = def_str;
    pkg.extended_default_string_field.length = (size_t)n2;

    if (seed & 2u) {
        pkg.extended_optional_default_float_field = (float)(seed * 0.25f);
        pkg.extended_optional_default_float_field_present = true;
    }
    if (seed & 4u) {
        static char opt_def_str[32];
        int n3 = snprintf(opt_def_str, sizeof(opt_def_str), "od-%u", seed);
        pkg.extended_optional_default_string_field.bytes = opt_def_str;
        pkg.extended_optional_default_string_field.length = (size_t)n3;
        pkg.extended_optional_default_string_field_present = true;
    }

    pkg.extended_no_default_u32_field = (uint32_t)(seed * 7u);
    static uint8_t ext_bytes[] = {0x11, 0x22, 0x33};
    pkg.extended_no_default_extern_field.bytes = ext_bytes;
    pkg.extended_no_default_extern_field.bit_length = 17u;

    if (seed & 8u) {
        pkg.extended_optional_no_default_u32_field = (uint32_t)(seed * 13u);
        pkg.extended_optional_no_default_u32_field_present = true;
    }
    if (seed & 16u) {
        static uint8_t opt_bytes[] = {0x44, 0x55, 0x66, 0x77};
        pkg.extended_optional_no_default_bytes_field.bytes = opt_bytes;
        pkg.extended_optional_no_default_bytes_field.length = 4u;
        pkg.extended_optional_no_default_bytes_field_present = true;
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = default_values_extended_default_values_extended_default_values_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
