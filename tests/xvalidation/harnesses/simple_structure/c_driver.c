#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "structure_types/simple_structure/simple_structure.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    structure_types_simple_structure_simple_structure_t pkg;
    structure_types_simple_structure_simple_structure_init(&pkg);
    uint32_t s = seed * 2654435761u + 1u;
    s = s * 1103515245u + 12345u; pkg.number_a = (uint8_t)((s >> 16) & 0x7u);
    s = s * 1103515245u + 12345u; pkg.number_b = (uint8_t)((s >> 16) & 0xffu);
    s = s * 1103515245u + 12345u; pkg.number_c = (uint8_t)((s >> 16) & 0x7fu);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = structure_types_simple_structure_simple_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
