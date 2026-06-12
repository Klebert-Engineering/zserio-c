/* Array element type is parameterized by a sibling field whose own
 * optional sub-field flows into the element. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/array_element_param_with_optional/holder.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_array_element_param_with_optional_holder_t pkg;
    parameterized_types_array_element_param_with_optional_holder_init(&pkg);

    const bool has_extra = (seed & 1u) ? true : false;
    pkg.param.has_extra = has_extra;
    if (has_extra) {
        pkg.param.extra_param = 0u; /* ExtraValue's `value if extraParam == 0` fires. */
        pkg.param.extra_param_present = true;
    }

    pkg.values_count = 3u;
    pkg.values = (parameterized_types_array_element_param_with_optional_value_t *)calloc(
            pkg.values_count, sizeof(*pkg.values));
    for (size_t i = 0; i < pkg.values_count; ++i) {
        parameterized_types_array_element_param_with_optional_value_init(&pkg.values[i]);
        if (has_extra) {
            pkg.values[i].extra_value.value = (uint64_t)((seed + (unsigned)i) * 1103u);
            pkg.values[i].extra_value.value_present = true;
            pkg.values[i].extra_value_present = true;
        } else {
            pkg.values[i].normal_value = (uint32_t)(seed * 191u + (unsigned)i);
            pkg.values[i].normal_value_present = true;
        }
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_array_element_param_with_optional_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
