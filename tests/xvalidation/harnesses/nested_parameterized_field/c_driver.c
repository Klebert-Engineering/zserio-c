/* Parameter cascade with a default value:
 *   ParamHolder { uint16 parameter = 11; Param(parameter) param; }
 *   Param(uint16 parameter) { uint16 value; uint32 extraValue if parameter == 11; }
 *
 * Driver pins parameter = 11 (matching the default) so extraValue is
 * always present, then varies value/extraValue. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/nested_parameterized_field/top_level.h"
#include "parameterized_types/nested_parameterized_field/param.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_nested_parameterized_field_top_level_t pkg;
    parameterized_types_nested_parameterized_field_top_level_init(&pkg);

    pkg.param_holder.parameter = 11u;
    parameterized_types_nested_parameterized_field_param_set_parameters(
            &pkg.param_holder.param, pkg.param_holder.parameter);
    pkg.param_holder.param.value = (uint16_t)(seed ^ 0xCAFEu);
    pkg.param_holder.param.extra_value_present = true;
    pkg.param_holder.param.extra_value = 0xDEADBEEFu ^ seed;

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_nested_parameterized_field_top_level_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
