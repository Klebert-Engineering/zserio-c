/* extern field at top level + inside nested struct, plus schema
 * functions returning extern. Wire format: just two extern values
 * (varsize length in bits, then the bits). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/structure_extern/test_structure.h"

static const uint8_t kBitsA[] = { 0x12, 0x34, 0x56 };
static const uint8_t kBitsB[] = { 0xAB, 0xCD };

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_structure_extern_test_structure_t pkg;
    functions_structure_extern_test_structure_init(&pkg);

    pkg.field.bytes = (uint8_t *)kBitsA;
    pkg.field.bit_length = 19u;  /* not byte-aligned, exercises tail bits */
    pkg.child.field.bytes = (uint8_t *)((seed & 1u) ? kBitsB : kBitsA);
    pkg.child.field.bit_length = (seed & 1u) ? 13u : 16u;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_structure_extern_test_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
