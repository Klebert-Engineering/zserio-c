/* Enum has a @removed item (REMOVED); only VALID is valid input on
 * read. We always set VALID. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "structure_types/structure_with_removed_enum_item_field/structure_with_removed_enum_item_field.h"
#include "structure_types/structure_with_removed_enum_item_field/enumeration.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    structure_types_structure_with_removed_enum_item_field_structure_with_removed_enum_item_field_t pkg;
    structure_types_structure_with_removed_enum_item_field_structure_with_removed_enum_item_field_init(&pkg);
    /* VALID = 1 in the schema (REMOVED=0 is @removed). */
    pkg.enumeration = 1u;
    (void)seed;

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = structure_types_structure_with_removed_enum_item_field_structure_with_removed_enum_item_field_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
