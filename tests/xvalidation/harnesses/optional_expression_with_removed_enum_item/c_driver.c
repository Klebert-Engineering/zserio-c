/* Optional array length expression references valueof(Numbers.TEN)
 * where TEN is `@removed` — generator must still evaluate the
 * constant in expressions even for removed enum items. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "optional_members/optional_expression_with_removed_enum_item/compound.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    optional_members_optional_expression_with_removed_enum_item_compound_t pkg;
    optional_members_optional_expression_with_removed_enum_item_compound_init(&pkg);

    /* Use len_base values both below 10 (optional absent) and >= 10. */
    static const uint32_t bases[] = {5u, 12u, 14u, 9u, 20u};
    const uint32_t base = bases[seed % 5u];
    pkg.len_base = base;
    if (base >= 10u) {
        const size_t n = (size_t)(base - 10u);
        pkg.array_count = n;
        pkg.array = (uint32_t *)calloc(n ? n : 1, sizeof(uint32_t));
        for (size_t i = 0; i < n; ++i)
            pkg.array[i] = (uint32_t)(seed * 7u + (unsigned)i);
        pkg.array_present = true;
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = optional_members_optional_expression_with_removed_enum_item_compound_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u base=%u bytes=%zu\n", seed, base, byte_pos);
    return 0;
}
