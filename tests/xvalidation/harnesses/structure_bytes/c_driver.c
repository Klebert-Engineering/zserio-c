/* bytes field at top level and inside a nested struct, plus a schema
 * function returning the nested bytes (function calls don't affect
 * wire format; their existence triggers extra packing-context entries
 * but the on-wire bits are just `bytes field; Child child;`). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/structure_bytes/test_structure.h"

static const uint8_t kBytesA[] = { 0x11, 0x22, 0x33, 0x44 };
static const uint8_t kBytesB[] = { 0xAA, 0xBB };

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_structure_bytes_test_structure_t pkg;
    functions_structure_bytes_test_structure_init(&pkg);

    pkg.field.bytes = (uint8_t *)kBytesA;
    pkg.field.length = sizeof(kBytesA);
    pkg.child.field.bytes = (uint8_t *)((seed & 1u) ? kBytesB : kBytesA);
    pkg.child.field.length = (seed & 1u) ? sizeof(kBytesB) : sizeof(kBytesA);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_structure_bytes_test_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
