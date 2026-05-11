/* `extend T extendedValue;` in a template — instantiate
 * `ExtendedCompound = Extended<Compound>` so the extend field's type
 * is a struct. Round-trips through compile-time template substitution
 * + extend byte-align. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "extended_members/extended_field_in_template/extended_compound.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_extended_field_in_template_extended_compound_t pkg;
    extended_members_extended_field_in_template_extended_compound_init(&pkg);
    pkg.value = 0xDEADBEEFu ^ seed;
    pkg.extended_value.field = 0xCAFEF00Du ^ (seed * 17u);

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_extended_field_in_template_extended_compound_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
