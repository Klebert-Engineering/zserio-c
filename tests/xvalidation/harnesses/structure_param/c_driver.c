/* Function on a parameterized inner struct (`metres` param used in
 * function body). The outer constraint `centimeters == toCentimetres()`
 * fixes centimeters = metres*100 = 200. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "functions/structure_param/metres_converter_caller.h"
#include "functions/structure_param/metres_converter.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    functions_structure_param_metres_converter_caller_t pkg;
    functions_structure_param_metres_converter_caller_init(&pkg);

    /* The schema fixes MetresConverter(2), so metres = 2. */
    functions_structure_param_metres_converter_set_parameters(&pkg.metres_converter, 2u);
    pkg.metres_converter.value_a = (uint16_t)(seed ^ 0xABCDu);
    pkg.centimeters = 200u;  /* constraint: 100 * metres */

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = functions_structure_param_metres_converter_caller_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
