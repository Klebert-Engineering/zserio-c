/* extend Compound — the extension field is a struct containing
 * an auto-array. Tests that compound (non-primitive) extend fields
 * round-trip through our extend byte alignment wrap. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "extended_members/extended_compound_field/extended.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    extended_members_extended_compound_field_extended_t pkg;
    extended_members_extended_compound_field_extended_init(&pkg);

    pkg.value = 0xDEADBEEFu ^ seed;
    pkg.extended_value.array_count = (size_t)((seed % 4u) + 1u);
    pkg.extended_value.array = (uint32_t *)calloc(pkg.extended_value.array_count, sizeof(uint32_t));
    for (size_t i = 0; i < pkg.extended_value.array_count; ++i)
        pkg.extended_value.array[i] = (uint32_t)(seed * 1103u + (unsigned)i);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = extended_members_extended_compound_field_extended_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
