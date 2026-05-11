/* Nested struct + string fields + schema function returning a
 * string literal. The string-literal-return case is the bug-fix:
 * `function string getConst() { return "POOL_CONST"; }` must wrap
 * the literal as a zserio_string_t. Wire is just two string fields. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/structure_string/test_structure.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_structure_string_test_structure_t pkg;
    functions_structure_string_test_structure_init(&pkg);
    const char *a = (seed & 1u) ? "POOL_FIELD" : "pool.alpha";
    pkg.pool.field.bytes = (char *)a;
    pkg.pool.field.length = strlen(a);
    const char *b = (seed & 1u) ? "FIELD" : "outer.beta";
    pkg.field.bytes = (char *)b;
    pkg.field.length = strlen(b);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_structure_string_test_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
