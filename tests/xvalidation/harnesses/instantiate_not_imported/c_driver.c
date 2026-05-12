/* Template `Test<T>` is declared in a separate package; the main
 * file imports the template name but NOT the `Test<uint32>`
 * instantiation alias `U32`. Generator must instantiate Test<uint32>
 * twice (once for the unimported U32 alias, once for the implicit
 * use here). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "templates/instantiate_not_imported/instantiate_not_imported.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_instantiate_not_imported_instantiate_not_imported_t pkg;
    templates_instantiate_not_imported_instantiate_not_imported_init(&pkg);

    pkg.test32.value = (uint32_t)(seed * 191u);
    static char text[32];
    int n = snprintf(text, sizeof(text), "imp-%u", seed);
    pkg.test_str.value.bytes = text;
    pkg.test_str.value.length = (size_t)n;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_instantiate_not_imported_instantiate_not_imported_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
