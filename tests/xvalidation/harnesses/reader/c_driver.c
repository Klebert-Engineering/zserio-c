/* Top-level package literally named `reader` — used by upstream to check
 * that a package name clashing with common parameter/variable names
 * still resolves. Our generator has no such conflict, but the wire
 * format check still applies. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "reader/test.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    reader_test_t pkg;
    reader_test_init(&pkg);

    pkg.array_count = (size_t)((seed % 3u) + 2u);
    pkg.array = (reader_element_t *)calloc(pkg.array_count, sizeof(*pkg.array));
    pkg.indexes_count = pkg.array_count;
    pkg.indexes = (uint32_t *)calloc(pkg.indexes_count, sizeof(uint32_t));
    for (size_t i = 0; i < pkg.array_count; ++i)
        pkg.array[i].field = (uint32_t)(seed * 7u + (unsigned)i);

    pkg.parameterized_array_count = (size_t)((seed % 3u) + 2u);
    pkg.parameterized_array = (reader_parameterized_element_t *)calloc(
            pkg.parameterized_array_count, sizeof(*pkg.parameterized_array));
    pkg.indexes_for_parameterized_count = pkg.parameterized_array_count;
    pkg.indexes_for_parameterized = (uint32_t *)calloc(
            pkg.indexes_for_parameterized_count, sizeof(uint32_t));
    for (size_t i = 0; i < pkg.parameterized_array_count; ++i)
    {
        /* Element parameter is @index; constraint requires field == index. */
        reader_parameterized_element_set_parameters(&pkg.parameterized_array[i], (uint32_t)i);
        pkg.parameterized_array[i].field = (uint32_t)i;
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = reader_test_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
