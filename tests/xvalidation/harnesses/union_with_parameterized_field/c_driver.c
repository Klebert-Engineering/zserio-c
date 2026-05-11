/* Union whose arm 1 is a parameterized struct ArrayHolder(10).
 * Schema fixes the parameter to 10 at the union arm, so the inner
 * struct always carries a 10-element uint32 array. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "union_types/union_with_parameterized_field/test_union.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    union_types_union_with_parameterized_field_test_union_t pkg;
    union_types_union_with_parameterized_field_test_union_init(&pkg);

    pkg.tag = seed & 1u;
    if (pkg.tag == 0u) {
        pkg.u.field = 0xDEADBEEFu ^ seed;
    } else {
        union_types_union_with_parameterized_field_array_holder_set_parameters(&pkg.u.array_holder, 10u);
        pkg.u.array_holder.array_count = 10;
        pkg.u.array_holder.array = (uint32_t *)calloc(10, sizeof(uint32_t));
        for (size_t i = 0; i < 10; ++i)
            pkg.u.array_holder.array[i] = (uint32_t)(seed * 1103u + (unsigned)i);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = union_types_union_with_parameterized_field_test_union_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u tag=%u bytes=%zu\n", seed, pkg.tag, byte_pos);
    return 0;
}
