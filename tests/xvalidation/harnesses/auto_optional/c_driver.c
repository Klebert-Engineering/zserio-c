#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "optional_members/auto_optional/container.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    optional_members_auto_optional_container_t pkg;
    optional_members_auto_optional_container_init(&pkg);
    uint32_t s = seed * 2654435761u + 1u;
    s = s * 1103515245u + 12345u; pkg.non_optional_int = (int32_t)(s & 0x7fffffff);
    /* Optional present iff seed is odd (gives the harness both branches). */
    if (seed & 1u) {
        pkg.auto_optional_int_present = true;
        s = s * 1103515245u + 12345u;
        pkg.auto_optional_int = (int32_t)(s & 0x7fffffff);
    } else {
        pkg.auto_optional_int_present = false;
    }

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = optional_members_auto_optional_container_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u present=%d bytes=%zu\n", seed,
            pkg.auto_optional_int_present ? 1 : 0, byte_pos);
    return 0;
}
