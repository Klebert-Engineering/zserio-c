/* Two optionals each with their own constraint:
 *   uint32 field;
 *   optional uint32 autoOptionalField : autoOptionalField > field;
 *   uint32 optionalField if field > 10 : optionalField > field;
 *
 * - autoOptionalField is auto-optional (1-bit prefix). When present
 *   must be strictly greater than `field`.
 * - optionalField is conditional on `field > 10`. When present
 *   must be strictly greater than `field`.
 *
 * Driver picks `field` to exercise the conditional both ways. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "constraints/optional_constraints/optional_constraints.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    constraints_optional_constraints_optional_constraints_t pkg;
    constraints_optional_constraints_optional_constraints_init(&pkg);

    /* Pick field — alternate between small (≤10, no optionalField) and
     * large (>10, optionalField present). */
    pkg.field = (seed & 1u) ? (uint32_t)100u : (uint32_t)5u;

    /* autoOptionalField: present iff seed bit 1 set. */
    pkg.auto_optional_field_present = (seed >> 1) & 1u;
    if (pkg.auto_optional_field_present) {
        pkg.auto_optional_field = pkg.field + 1u + (uint32_t)(seed & 0xFFu);
    }

    /* optionalField: presence is dictated by field > 10, not by caller. */
    if (pkg.field > 10u) {
        pkg.optional_field_present = true;
        pkg.optional_field = pkg.field + 1u + (uint32_t)((seed * 7u) & 0xFFu);
    }

    uint8_t buf[32];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = constraints_optional_constraints_optional_constraints_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u f=%u ao?%d o?%d bytes=%zu\n",
            seed, pkg.field, pkg.auto_optional_field_present, pkg.optional_field_present, byte_pos);
    return 0;
}
