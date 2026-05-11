/* Implicit `Test<uint32>` use co-exists with an explicit `instantiate
 * Test<string> Test_uint32` of a DIFFERENT instantiation. The generator
 * must NOT collapse them into the same generated type. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "templates/instantiate_clash_other_template/instantiate_clash_other_template.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    templates_instantiate_clash_other_template_instantiate_clash_other_template_t pkg;
    templates_instantiate_clash_other_template_instantiate_clash_other_template_init(&pkg);

    pkg.test.value = (uint32_t)(seed * 191u);

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_instantiate_clash_other_template_instantiate_clash_other_template_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
