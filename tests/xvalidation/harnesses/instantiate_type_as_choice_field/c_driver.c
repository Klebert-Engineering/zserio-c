/* Choice arm holding a template-instantiated struct (Test32 = Test<uint32>). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "templates/instantiate_type_as_choice_field/instantiate_type_as_choice_field.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    const bool sel = (seed & 1u) ? true : false;

    templates_instantiate_type_as_choice_field_instantiate_type_as_choice_field_t pkg;
    templates_instantiate_type_as_choice_field_instantiate_type_as_choice_field_init(&pkg);
    templates_instantiate_type_as_choice_field_instantiate_type_as_choice_field_set_parameters(&pkg, sel);
    if (sel) pkg.u.test.value = (uint32_t)(seed * 11u);
    else     pkg.u.value      = (uint32_t)(seed * 13u);

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = templates_instantiate_type_as_choice_field_instantiate_type_as_choice_field_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u sel=%d bytes=%zu\n", seed, (int)sel, byte_pos);
    return 0;
}
