/* Multi-package import: package_import.top imports
 * package_import.first.* and .second.*, and uses Coordinate from
 * all three packages (the local one has 2 parameters; the imported
 * `first` has 0; the imported `second` has 1). Verifies that
 * cross-package type resolution + the right parameter shape per
 * call site survives codegen. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "package_import/top/top_structure.h"
#include "package_import/top/coordinate.h"
#include "package_import/second/coordinate.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    package_import_top_top_structure_t pkg;
    package_import_top_top_structure_init(&pkg);

    /* Local Coordinate(1, 1): constraints fix coord_x=1, coord_y=1. */
    package_import_top_coordinate_set_parameters(&pkg.coordinate_local, 1u, 1u);
    pkg.coordinate_local.coord_x = 1u;
    pkg.coordinate_local.coord_y = 1u;

    /* Imported first.Coordinate: free uint32 fields. */
    pkg.coordinate_imported_first.coord_x = 0xDEADBEEFu ^ seed;
    pkg.coordinate_imported_first.coord_y = 0x12345678u ^ (seed * 7u);

    /* Imported second.Coordinate(1): bit<1> coord_x, coord_y. */
    package_import_second_coordinate_set_parameters(&pkg.coordinate_imported_second, 1u);
    pkg.coordinate_imported_second.coord_x = (uint64_t)(seed & 1u);
    pkg.coordinate_imported_second.coord_y = (uint64_t)((seed >> 1) & 1u);

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = package_import_top_top_structure_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
