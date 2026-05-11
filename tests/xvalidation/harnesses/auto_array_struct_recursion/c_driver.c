/* Recursive struct: AutoArrayRecursion has an array of itself.
 * Driver builds a tiny tree to verify both empty and non-empty
 * recursive arrays serialise correctly. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/auto_array_struct_recursion/auto_array_recursion.h"

typedef array_types_auto_array_struct_recursion_auto_array_recursion_t Node;

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);

    Node root;
    array_types_auto_array_struct_recursion_auto_array_recursion_init(&root);
    root.id = (uint8_t)(seed & 0xFFu);

    /* Two children, the first of which has one grandchild. */
    root.auto_array_recursion_count = 2;
    root.auto_array_recursion = (Node *)calloc(2, sizeof(Node));
    for (int i = 0; i < 2; ++i) {
        array_types_auto_array_struct_recursion_auto_array_recursion_init(&root.auto_array_recursion[i]);
        root.auto_array_recursion[i].id = (uint8_t)((seed + 1u + (unsigned)i) & 0xFFu);
    }
    /* Grandchild on first child. */
    root.auto_array_recursion[0].auto_array_recursion_count = 1;
    root.auto_array_recursion[0].auto_array_recursion = (Node *)calloc(1, sizeof(Node));
    array_types_auto_array_struct_recursion_auto_array_recursion_init(
            &root.auto_array_recursion[0].auto_array_recursion[0]);
    root.auto_array_recursion[0].auto_array_recursion[0].id = (uint8_t)((seed * 7u) & 0xFFu);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_auto_array_struct_recursion_auto_array_recursion_write(&root, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
