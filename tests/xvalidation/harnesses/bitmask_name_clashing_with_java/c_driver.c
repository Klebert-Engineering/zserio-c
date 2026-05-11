/* Bitmask named "String" — schema chose this on purpose to clash with
 * java.lang.String. Our C codegen lowers it to
 * `identifiers_bitmask_name_clashing_with_java_string_t`, no clash
 * possible. Wire payload is a single uint8 (the bitmask). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "identifiers/bitmask_name_clashing_with_java/bitmask_name_clashing_with_java.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    identifiers_bitmask_name_clashing_with_java_bitmask_name_clashing_with_java_t pkg;
    identifiers_bitmask_name_clashing_with_java_bitmask_name_clashing_with_java_init(&pkg);
    pkg.string_field = (uint8_t)((seed * 3u) & 0x03u);  /* bitmask String has READ=1, WRITE=2 */

    uint8_t buf[8];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = identifiers_bitmask_name_clashing_with_java_bitmask_name_clashing_with_java_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
