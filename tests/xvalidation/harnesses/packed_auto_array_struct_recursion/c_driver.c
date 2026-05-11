/* Recursive struct as the element type of a packed auto-array.
 * Forces the generator to emit both (a) self-referential typedef,
 * (b) packed-array codegen using the same compound's own
 * read_packed/write_packed/observe helpers. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/packed_auto_array_struct_recursion/packed_auto_array_recursion.h"

typedef array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_t Node;

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    Node root;
    array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_init(&root);
    root.id = (uint8_t)(seed & 0xFFu);

    /* Three children for delta encoding to have something to compress. */
    root.packed_auto_array_recursion_count = 3;
    root.packed_auto_array_recursion = (Node *)calloc(3, sizeof(Node));
    for (int i = 0; i < 3; ++i) {
        array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_init(
                &root.packed_auto_array_recursion[i]);
        root.packed_auto_array_recursion[i].id = (uint8_t)((seed + 10u + (unsigned)i * 2u) & 0xFFu);
    }
    /* Grandchildren on the second child (two of them). */
    root.packed_auto_array_recursion[1].packed_auto_array_recursion_count = 2;
    root.packed_auto_array_recursion[1].packed_auto_array_recursion = (Node *)calloc(2, sizeof(Node));
    for (int i = 0; i < 2; ++i) {
        array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_init(
                &root.packed_auto_array_recursion[1].packed_auto_array_recursion[i]);
        root.packed_auto_array_recursion[1].packed_auto_array_recursion[i].id =
                (uint8_t)((seed * 7u + (unsigned)i) & 0xFFu);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st =
        array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_write(&root, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
