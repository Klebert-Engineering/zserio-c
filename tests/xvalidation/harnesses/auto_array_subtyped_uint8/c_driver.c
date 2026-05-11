/* Subtype-wrapped uint8 element type. Wire format is just uint8
 * per element; the subtype is transparent. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/auto_array_subtyped_uint8/auto_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_auto_array_subtyped_uint8_auto_array_t pkg;
    array_types_auto_array_subtyped_uint8_auto_array_init(&pkg);

    pkg.array_count = (size_t)((seed % 7u) + 1u);
    pkg.array = (array_types_auto_array_subtyped_uint8_array_element_t *)calloc(
            pkg.array_count, sizeof(*pkg.array));
    for (size_t i = 0; i < pkg.array_count; ++i)
        pkg.array[i] = (uint8_t)(seed * 17u + (unsigned)i);

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_auto_array_subtyped_uint8_auto_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.array); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%zu bytes=%zu\n", seed, pkg.array_count, byte_pos);
    free(pkg.array);
    return 0;
}
