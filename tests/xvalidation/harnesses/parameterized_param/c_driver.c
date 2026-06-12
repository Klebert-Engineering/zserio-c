/* Two-level parameterized chain: Param(uint16) and ParameterizedParam(Param). */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/parameterized_param/parameterized_param_holder.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_parameterized_param_parameterized_param_holder_t pkg;
    parameterized_types_parameterized_param_parameterized_param_holder_init(&pkg);

    pkg.parameter = 11u; /* keep the optional `extraValue if parameter == 11` armed. */
    pkg.param.value = (uint16_t)((seed * 3u) & 0xFFFFu);
    pkg.param.extra_value = (uint32_t)(seed * 17u);
    pkg.parameterized_param.value = (uint16_t)((seed * 5u) & 0xFFFFu);
    pkg.parameterized_param.extra_value = (uint32_t)(seed * 23u);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_parameterized_param_parameterized_param_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
