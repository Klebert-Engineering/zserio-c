/* nonOptional bool + auto-optional bool. Wire: 1-bit nonOpt + 1-bit
 * presence prefix + (if present) 1-bit value. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "optional_members/auto_optional_bool/container.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    optional_members_auto_optional_bool_container_t pkg;
    optional_members_auto_optional_bool_container_init(&pkg);
    pkg.non_optional_bool = (seed & 1u) != 0u;
    pkg.auto_optional_bool_present = (seed & 2u) != 0u;
    if (pkg.auto_optional_bool_present)
        pkg.auto_optional_bool = (seed & 4u) != 0u;

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = optional_members_auto_optional_bool_container_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
