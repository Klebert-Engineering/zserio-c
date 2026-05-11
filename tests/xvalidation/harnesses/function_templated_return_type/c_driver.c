/* Templated struct with a function returning the template parameter type T. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "templates/function_templated_return_type/function_templated_return_type.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_function_templated_return_type_function_templated_return_type_t pkg;
    templates_function_templated_return_type_function_templated_return_type_init(&pkg);

    pkg.has_holder = (seed & 1u) ? true : false;
    if (pkg.has_holder) {
        pkg.uint32_test.holder.value = (uint32_t)(seed * 17u);
        static char text[32]; int n = snprintf(text, sizeof(text), "h-%u", seed);
        pkg.string_test.holder.value.bytes = text; pkg.string_test.holder.value.length = (size_t)n;
    } else {
        pkg.uint32_test.value = (uint32_t)(seed * 23u);
        static char text[32]; int n = snprintf(text, sizeof(text), "v-%u", seed);
        pkg.string_test.value.bytes = text; pkg.string_test.value.length = (size_t)n;
    }
    /* floatTest is constructed with (false), so always emits the value branch. */
    pkg.float_test.value = (float)(seed * 0.125f);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_function_templated_return_type_function_templated_return_type_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
