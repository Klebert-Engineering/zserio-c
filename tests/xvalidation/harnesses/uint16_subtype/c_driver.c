/* Subtype: `subtype uint16 Identifier;` then a struct containing
 * (Identifier, string). The subtype is transparent at the wire level. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "subtypes/uint16_subtype/test_structure.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    subtypes_uint16_subtype_test_structure_t pkg;
    subtypes_uint16_subtype_test_structure_init(&pkg);
    pkg.identifier = (subtypes_uint16_subtype_identifier_t)(seed * 1103u + 1u);
    const char *name = (seed & 1u) ? "subtype.name" : "another-name";
    pkg.name.bytes = (char *)name;
    pkg.name.length = strlen(name);

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = subtypes_uint16_subtype_test_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u id=%u bytes=%zu\n", seed, (unsigned)pkg.identifier, byte_pos);
    return 0;
}
