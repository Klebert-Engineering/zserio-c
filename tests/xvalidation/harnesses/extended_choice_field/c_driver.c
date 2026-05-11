/* `extend Choice(numElements) extendedValue;` — extend field is a
 * parameterized choice. Choice arms: case 0 empty, case 1 uint32
 * value, default uint32 values[numElements]. Driver tests all three
 * arms by varying numElements per seed. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "extended_members/extended_choice_field/extended.h"
#include "extended_members/extended_choice_field/choice.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_extended_choice_field_extended_t pkg;
    extended_members_extended_choice_field_extended_init(&pkg);

    /* numElements: 0=empty, 1=value, else=values array. */
    const uint32_t n = (uint32_t)(seed % 3u);
    pkg.num_elements = (n == 2u) ? 3u : n;
    extended_members_extended_choice_field_choice_set_parameters(&pkg.extended_value, pkg.num_elements);
    if (pkg.num_elements == 1u) {
        pkg.extended_value.u.value = 0xDEADBEEFu ^ seed;
    } else if (pkg.num_elements >= 2u) {
        pkg.extended_value.values_count = pkg.num_elements;
        pkg.extended_value.u.values = (uint32_t *)calloc(pkg.num_elements, sizeof(uint32_t));
        for (uint32_t i = 0; i < pkg.num_elements; ++i)
            pkg.extended_value.u.values[i] = (uint32_t)(seed * 1103u + i);
    }
    /* numElements == 0 → empty case; nothing to set. */

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_extended_choice_field_extended_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%u bytes=%zu\n", seed, pkg.num_elements, byte_pos);
    return 0;
}
