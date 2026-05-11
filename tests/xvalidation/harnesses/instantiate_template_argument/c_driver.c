/* Nested template: Other<Test<string>>; instantiate Test<string> as Str.
 * Generated type chain: pkg.other (Other_Str) -> .value (Str) -> .value (string). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "templates/instantiate_template_argument/instantiate_template_argument.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_instantiate_template_argument_instantiate_template_argument_t pkg;
    templates_instantiate_template_argument_instantiate_template_argument_init(&pkg);

    static char text[64];
    int n = snprintf(text, sizeof(text), "tmpl-%u", seed);
    pkg.other.value.value.bytes = text;
    pkg.other.value.value.length = (size_t)n;

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_instantiate_template_argument_instantiate_template_argument_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
