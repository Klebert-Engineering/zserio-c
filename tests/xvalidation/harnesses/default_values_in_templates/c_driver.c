/* Template structs with default values per scalar/string family. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "default_values/default_values_in_templates/default_values_in_template.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    default_values_default_values_in_templates_default_values_in_template_t pkg;
    default_values_default_values_in_templates_default_values_in_template_init(&pkg);

    pkg.b_default.field = (seed & 1u) ? true : false;
    pkg.u32_default.field = (uint32_t)(seed * 11u);
    pkg.varint_default.field = (int64_t)((int)seed - 5000);
    pkg.i7_default.field = (int8_t)((int)((seed % 127u)) - 63);
    pkg.f16_default.field = (float)(seed * 0.5f);
    pkg.f64_default.field = (double)(seed * 0.125);

    static char text[32];
    int n = snprintf(text, sizeof(text), "s-%u", seed);
    pkg.s_default.field.bytes = text;
    pkg.s_default.field.length = (size_t)n;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = default_values_default_values_in_templates_default_values_in_template_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
