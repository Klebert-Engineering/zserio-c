/* Template instantiation: `instantiate Test<uint32> U32;` produces a
 * generated type whose layout is `Test<uint32>` (just a uint32 field).
 * The wrapping struct `InstantiateSimple { U32 test; }` therefore writes
 * a single uint32 on the wire. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "templates/instantiate_simple/instantiate_simple.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_instantiate_simple_instantiate_simple_t pkg;
    templates_instantiate_simple_instantiate_simple_init(&pkg);
    pkg.test.value = 0xCAFEF00Du ^ seed;

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_instantiate_simple_instantiate_simple_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u value=%u bytes=%zu\n", seed, pkg.test.value, byte_pos);
    return 0;
}
