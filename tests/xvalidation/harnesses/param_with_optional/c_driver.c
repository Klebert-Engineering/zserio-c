/* Chained parameter + optional fields. Schema layout:
 *
 *   Holder { Param param; Value(param) value; };
 *   Param { bool hasExtra; bit:7 extraParam if hasExtra; };
 *   Value(Param param) {
 *       uint32 normalValue if !param.hasExtra;
 *       ExtraValue(param.extraParam) extraValue if param.hasExtra;
 *   };
 *   ExtraValue(bit:7 extraParam) { uint64 value if extraParam == 0; };
 *
 * Three reachable shapes:
 *  - hasExtra=false → param=1b + normalValue (32b)
 *  - hasExtra=true, extraParam=0  → param=8b + ExtraValue.value (64b)
 *  - hasExtra=true, extraParam!=0 → param=8b + ExtraValue is empty
 */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/param_with_optional/holder.h"
#include "parameterized_types/param_with_optional/extra_value.h"
#include "parameterized_types/param_with_optional/value.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_param_with_optional_holder_t pkg;
    parameterized_types_param_with_optional_holder_init(&pkg);

    const unsigned mode = seed % 3u;  /* 0,1,2 = the three shapes */
    pkg.param.has_extra = (mode != 0u);
    if (pkg.param.has_extra) {
        pkg.param.extra_param_present = true;
        pkg.param.extra_param = (mode == 1u) ? 0u : (uint8_t)((seed & 0x7Fu) | 1u);
    }
    /* Value carries param as its parameter. Internally it stores a
     * copy of the parameter (per our codegen's parameter-storage
     * convention). */
    pkg.value.param = pkg.param;
    if (!pkg.param.has_extra) {
        pkg.value.normal_value_present = true;
        pkg.value.normal_value = 0xDEADBEEFu ^ seed;
    } else {
        pkg.value.extra_value_present = true;
        pkg.value.extra_value.extra_param = pkg.param.extra_param;
        if (pkg.param.extra_param == 0u) {
            pkg.value.extra_value.value_present = true;
            pkg.value.extra_value.value = 0x0123456789ABCDEFull ^ (uint64_t)seed;
        }
    }

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_param_with_optional_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u mode=%u bytes=%zu\n", seed, mode, byte_pos);
    return 0;
}
