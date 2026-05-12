/* Self-referential Block — `Block nextData if blockTerminator > 0`
 * uses heap indirection (generator emits `Block *next_data`). Build
 * a chain of 3 blocks (terminated by blockTerminator=0). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/allocator.h>
#include "optional_members/optional_recursion/block.h"

extern zserio_allocator_t zserio_std_allocator;

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);

    optional_members_optional_recursion_block_t b0;
    optional_members_optional_recursion_block_init(&b0);
    optional_members_optional_recursion_block_set_parameters(&b0, 3u); /* outer byteCount = 3 */
    b0.data_bytes_count = 3u;
    static uint8_t data0[3];
    data0[0] = (uint8_t)(seed & 0xFFu);
    data0[1] = (uint8_t)((seed * 3u) & 0xFFu);
    data0[2] = (uint8_t)((seed * 7u) & 0xFFu);
    b0.data_bytes = data0;
    b0.block_terminator = 2u; /* points to next block with byteCount 2 */

    optional_members_optional_recursion_block_t b1;
    optional_members_optional_recursion_block_init(&b1);
    optional_members_optional_recursion_block_set_parameters(&b1, 2u);
    b1.data_bytes_count = 2u;
    static uint8_t data1[2];
    data1[0] = (uint8_t)((seed * 11u) & 0xFFu);
    data1[1] = (uint8_t)((seed * 13u) & 0xFFu);
    b1.data_bytes = data1;
    b1.block_terminator = 1u;

    optional_members_optional_recursion_block_t b2;
    optional_members_optional_recursion_block_init(&b2);
    optional_members_optional_recursion_block_set_parameters(&b2, 1u);
    b2.data_bytes_count = 1u;
    static uint8_t data2[1];
    data2[0] = (uint8_t)((seed * 17u) & 0xFFu);
    b2.data_bytes = data2;
    b2.block_terminator = 0u; /* terminate */

    /* Link the chain through the heap-pointer fields. */
    b0.next_data = &b1;
    b0.next_data_present = true;
    b1.next_data = &b2;
    b1.next_data_present = true;

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = optional_members_optional_recursion_block_write(&b0, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
