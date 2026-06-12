/* Parameterized struct: `Item(uint32 version)` with a conditional field
 * `uint32 extraParam if version >= 10`. Both sides must drive `version`
 * from the seed (it is not on the wire); cpp_driver receives it via
 * argv[3]. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/simple_param/item.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    /* Drive version to exercise both branches of `if version >= 10`. */
    const uint32_t version = (seed & 1u) ? (uint32_t)(20u + (seed % 30u))
                                         : (uint32_t)(seed % 10u);

    parameterized_types_simple_param_item_t pkg;
    parameterized_types_simple_param_item_init(&pkg);
    parameterized_types_simple_param_item_set_parameters(&pkg, version);
    pkg.param = (uint16_t)((seed * 1103u) + 13u);
    pkg.extra_param_present = (version >= 10u);
    pkg.extra_param = pkg.extra_param_present ? (uint32_t)(seed * 2654435761u + 1u) : 0u;

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_simple_param_item_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u version=%u bytes=%zu\n", seed, version, byte_pos);
    return 0;
}
