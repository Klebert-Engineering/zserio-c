/* Top-level package literally named `index`. The xvalidation runner
 * applies `-setTopLevelPackage index_workaround` to dodge the cpp-side
 * collision with libc's `index()` (pulled in by <strings.h>); after
 * that remap, our generated symbols are prefixed accordingly.
 *
 * The Test struct exercises two parallel indexed-offset arrays:
 *   - `indexes[i]` carries the byte offset of `array[i]` (Element).
 *   - `indexes_for_parameterized[i]` carries the byte offset of
 *     `parameterized_array[i]` (ParameterizedElement with constraint
 *     `field == index`).
 * Offset values are auto-back-patched by the runtime; the driver
 * leaves them zero. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "index_workaround/index/test.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    index_workaround_index_test_t pkg;
    index_workaround_index_test_init(&pkg);

    /* Vary 1..5 elements per array. */
    const size_t n = (size_t)((seed % 5u) + 1u);
    pkg.indexes_count = n;
    pkg.indexes = (uint32_t *)calloc(n, sizeof(uint32_t));
    pkg.array_count = n;
    pkg.array = (index_workaround_index_element_t *)calloc(
            n, sizeof(*pkg.array));
    pkg.indexes_for_parameterized_count = n;
    pkg.indexes_for_parameterized = (uint32_t *)calloc(n, sizeof(uint32_t));
    pkg.parameterized_array_count = n;
    pkg.parameterized_array = (index_workaround_index_parameterized_element_t *)calloc(
            n, sizeof(*pkg.parameterized_array));
    if (pkg.indexes == NULL || pkg.array == NULL ||
        pkg.indexes_for_parameterized == NULL || pkg.parameterized_array == NULL)
    {
        fprintf(stderr, "alloc\n"); return 1;
    }

    for (size_t i = 0; i < n; ++i) {
        pkg.array[i].field = (uint32_t)(seed * 191u + (unsigned)i);
        /* The schema's `field : field == index` constraint demands
         * that parameterized_array[i].field equals its array index. */
        pkg.parameterized_array[i].index = (uint32_t)i;
        pkg.parameterized_array[i].field = (uint32_t)i;
    }

    uint8_t buf[512];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = index_workaround_index_test_write(&pkg, &w);
    if (st != ZSERIO_OK) {
        fprintf(stderr, "write: %d\n", (int)st);
        free(pkg.indexes); free(pkg.array);
        free(pkg.indexes_for_parameterized); free(pkg.parameterized_array);
        return 1;
    }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%zu bytes=%zu\n", seed, n, byte_pos);
    free(pkg.indexes); free(pkg.array);
    free(pkg.indexes_for_parameterized); free(pkg.parameterized_array);
    return 0;
}
