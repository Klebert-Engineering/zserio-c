/* `Extended<T>` template with `extend optional T extendedValue`,
 * instantiated as ExtendedSimple (T = uint32). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "extended_members/extended_optional_field_in_template/extended_simple.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_extended_optional_field_in_template_extended_simple_t pkg;
    extended_members_extended_optional_field_in_template_extended_simple_init(&pkg);

    pkg.value = (uint32_t)(seed * 191u);
    if (seed & 1u) {
        pkg.extended_value = (uint32_t)(seed * 13u);
        pkg.extended_value_present = true;
    }

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_extended_optional_field_in_template_extended_simple_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
