/* Template instantiation across packages: `instantiate Test<uint32> U32;`
 * lives in the imported `pkg` subpackage, while Test<string> is
 * instantiated locally. Verifies cross-package template-instantiated
 * type references. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "templates/instantiate_imported/instantiate_imported.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_instantiate_imported_instantiate_imported_t pkg;
    templates_instantiate_imported_instantiate_imported_init(&pkg);

    pkg.test32.value = 0xCAFEF00Du ^ seed;
    const char *s = (seed & 1u) ? "imported.template" : "hello there";
    pkg.test_str.value.bytes = (char *)s;
    pkg.test_str.value.length = strlen(s);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_instantiate_imported_instantiate_imported_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
