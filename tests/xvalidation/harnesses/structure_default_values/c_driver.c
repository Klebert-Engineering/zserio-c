/* Struct with default-value initializers. Defaults are a generator
 * concern (init should pre-fill them), not a wire concern — every
 * field is always serialized regardless. This driver sets every field
 * explicitly, so the wire bytes don't depend on whether init applied
 * defaults or not. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "default_values/structure_default_values/structure_default_values.h"
#include "default_values/structure_default_values/basic_color.h"
#include "default_values/structure_default_values/permission.h"
#include "default_values/structure_default_values/nibble.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    default_values_structure_default_values_structure_default_values_t pkg;
    default_values_structure_default_values_structure_default_values_init(&pkg);

    pkg.bool_value = true;
    pkg.bit4_value_present = true;
    pkg.bit4_value = (default_values_structure_default_values_nibble_t)((seed * 5u) & 0x0Fu);
    pkg.int16_value = (int16_t)(-2000 + (int)(seed % 4000u));
    pkg.float16_value = 1.5f + (float)(seed % 3u);
    pkg.float32_value = 2.5f + (float)(seed % 7u);
    pkg.float64_value = 3.5 + (double)(seed % 11u);
    const char *s = (seed & 1u) ? "default" : "values";
    pkg.string_value.bytes = (char *)s;
    pkg.string_value.length = strlen(s);
    pkg.enum_value = (default_values_structure_default_values_basic_color_t)(seed % 3u);
    pkg.bitmask_value = (default_values_structure_default_values_permission_t)((seed * 3u) & 0x3Fu);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = default_values_structure_default_values_structure_default_values_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
