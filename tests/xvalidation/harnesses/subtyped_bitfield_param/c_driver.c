/* Parameter is a subtype (bit:5 ParamType) defined AFTER the struct
 * that uses it — exercises evaluation-phase ordering. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/subtyped_bitfield_param/subtyped_bitfield_param_holder.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_subtyped_bitfield_param_subtyped_bitfield_param_holder_t pkg;
    parameterized_types_subtyped_bitfield_param_subtyped_bitfield_param_holder_init(&pkg);

    /* Param literal is 11 in the schema; the optional fires when param == 11. */
    pkg.subtyped_bitfield_param.value = (uint16_t)((seed * 7u) & 0xFFFFu);
    pkg.subtyped_bitfield_param.extra_value = (uint32_t)(seed * 43u);

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_subtyped_bitfield_param_subtyped_bitfield_param_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
