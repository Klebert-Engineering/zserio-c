/* AutoArrayHolder layout:
 *   uint32 autoArrayOffset;
 *   bit:1  forceAlignment;          // forces non-byte alignment so the
 *                                    // offset directive's pad isn't a no-op
 *   autoArrayOffset:
 *   int:7  autoArray[];             // varsize length + N*7-bit elements
 *
 * Driver fills only the data; the writer back-patches the offset. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "offsets/auto_array_offset/auto_array_holder.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    offsets_auto_array_offset_auto_array_holder_t pkg;
    offsets_auto_array_offset_auto_array_holder_init(&pkg);

    pkg.force_alignment = 1u;
    pkg.auto_array_count = (size_t)((seed % 6u) + 1u);
    pkg.auto_array = (int8_t *)calloc(pkg.auto_array_count, sizeof(int8_t));
    if (pkg.auto_array == NULL) { fprintf(stderr, "alloc\n"); return 1; }
    for (size_t i = 0; i < pkg.auto_array_count; ++i) {
        /* int:7 elements: range -64..63 */
        pkg.auto_array[i] = (int8_t)((int)((seed * 17u + (unsigned)i) % 128u) - 64);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = offsets_auto_array_offset_auto_array_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.auto_array); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%zu off=%u bytes=%zu\n",
            seed, pkg.auto_array_count, (unsigned)pkg.auto_array_offset, byte_pos);
    free(pkg.auto_array);
    return 0;
}
