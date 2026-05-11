/* Dynamic bit-width field passed as a parameter to a nested struct:
 *   bit:4 length = 5; int<length> bitfield = 11;
 *   DynamicBitfieldParam(bitfield) dynamicBitfieldParam;
 *
 * Inner struct gates `extraValue` on `param == 11`. We choose
 * bitfield=11 so the extra field is present. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "parameterized_types/dynamic_bitfield_param/dynamic_bitfield_param_holder.h"
#include "parameterized_types/dynamic_bitfield_param/dynamic_bitfield_param.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    parameterized_types_dynamic_bitfield_param_dynamic_bitfield_param_holder_t pkg;
    parameterized_types_dynamic_bitfield_param_dynamic_bitfield_param_holder_init(&pkg);

    pkg.length = 5u;  /* matches schema default — int:5 range -16..15 */
    pkg.bitfield = 11;
    parameterized_types_dynamic_bitfield_param_dynamic_bitfield_param_set_parameters(
            &pkg.dynamic_bitfield_param, (int)pkg.bitfield);
    pkg.dynamic_bitfield_param.value = (uint16_t)(seed * 17u);
    pkg.dynamic_bitfield_param.extra_value_present = true;
    pkg.dynamic_bitfield_param.extra_value = 0xDEADBEEFu ^ seed;

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = parameterized_types_dynamic_bitfield_param_dynamic_bitfield_param_holder_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
