/* Indexed offset directive `offsets[@index]:`.
 *
 * `offsets` is a uint32 auto-array; `data` is a bit:5 auto-array
 * whose every element is byte-aligned and back-patched into the
 * matching `offsets[i]`. The driver doesn't have to set offset
 * values — the generator's per-element back-patch fills them. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "indexed_offsets/auto_indexed_offset_array/auto_indexed_offset_array.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    indexed_offsets_auto_indexed_offset_array_auto_indexed_offset_array_t pkg;
    indexed_offsets_auto_indexed_offset_array_auto_indexed_offset_array_init(&pkg);

    /* Pick 1..6 elements; offsets[] and data[] must agree in count. */
    const size_t n = (size_t)((seed % 6u) + 1u);
    pkg.offsets_count = n;
    pkg.offsets = (uint32_t *)calloc(n, sizeof(uint32_t));
    pkg.data_count = n;
    pkg.data = (uint8_t *)calloc(n, sizeof(uint8_t));
    if (pkg.offsets == NULL || pkg.data == NULL) { fprintf(stderr, "alloc\n"); return 1; }

    pkg.spacer = 1u;  /* force non-byte alignment before the data array */
    for (size_t i = 0; i < n; ++i) {
        pkg.data[i] = (uint8_t)((seed * 17u + (unsigned)i) & 0x1Fu);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = indexed_offsets_auto_indexed_offset_array_auto_indexed_offset_array_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.offsets); free(pkg.data); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%zu bytes=%zu offs[0]=%u\n",
            seed, n, byte_pos, n > 0 ? (unsigned)pkg.offsets[0] : 0u);
    free(pkg.offsets);
    free(pkg.data);
    return 0;
}
