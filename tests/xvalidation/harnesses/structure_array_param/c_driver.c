/* Schema function used as parameter expression for array elements:
 *   ChildStructure(getChildBitSize())          children[numChildren];
 *   ChildStructure(notLeftMost.getAnotherChildBitSize())
 *                                              anotherChildren[numAnotherChildren];
 *
 * getChildBitSize() returns 19 (constant); getAnotherChildBitSize()
 * returns 17. Element bit widths are therefore 19 and 17 bits. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/structure_array_param/parent_structure.h"
#include "functions/structure_array_param/child_structure.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_structure_array_param_parent_structure_t pkg;
    functions_structure_array_param_parent_structure_init(&pkg);

    pkg.num_children = (uint8_t)((seed % 4u) + 1u);
    pkg.children_count = pkg.num_children;
    pkg.children = (functions_structure_array_param_child_structure_t *)calloc(
            pkg.children_count, sizeof(*pkg.children));
    for (size_t i = 0; i < pkg.children_count; ++i) {
        functions_structure_array_param_child_structure_set_parameters(&pkg.children[i], 19u);
        /* bit:19 range 0..524287 */
        pkg.children[i].value = (uint64_t)(((seed + (unsigned)i) * 1103u) & 0x7FFFFu);
    }

    pkg.num_another_children = (uint8_t)((seed % 3u) + 2u);
    pkg.another_children_count = pkg.num_another_children;
    pkg.another_children = (functions_structure_array_param_child_structure_t *)calloc(
            pkg.another_children_count, sizeof(*pkg.another_children));
    for (size_t i = 0; i < pkg.another_children_count; ++i) {
        functions_structure_array_param_child_structure_set_parameters(&pkg.another_children[i], 17u);
        /* bit:17 range 0..131071 */
        pkg.another_children[i].value = (uint64_t)(((seed * 7u + (unsigned)i)) & 0x1FFFFu);
    }

    uint8_t buf[128];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_structure_array_param_parent_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u nC=%u nA=%u bytes=%zu\n",
            seed, (unsigned)pkg.num_children, (unsigned)pkg.num_another_children, byte_pos);
    return 0;
}
