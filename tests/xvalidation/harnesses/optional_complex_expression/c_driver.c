/* Conditional optional with a multi-clause boolean expression:
 *   extraData if (permission & WRITE) == WRITE
 *           || (permission & APPEND) == APPEND
 *           || (permission & CREATE) == CREATE
 * Exercises the bitwise-paren wrap from the earlier expression fix. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "optional_members/optional_complex_expression/container.h"
#include "optional_members/optional_complex_expression/permission.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    optional_members_optional_complex_expression_container_t pkg;
    optional_members_optional_complex_expression_container_init(&pkg);

    /* Walk a few combinations: alternate between
     * NONE / READ-only (no extraData) and various bits set (extraData present). */
    static const uint8_t kPermSet[] = {
        0x00u,  /* NONE — no extraData */
        0x02u,  /* READ — no extraData */
        0x04u,  /* WRITE — extraData */
        0x08u,  /* APPEND — extraData */
        0x10u,  /* CREATE — extraData */
        0x14u,  /* WRITE|CREATE */
    };
    const uint8_t perm = kPermSet[seed % (sizeof(kPermSet) / sizeof(kPermSet[0]))];
    pkg.permission = perm;
    /* Determine presence the same way the schema does — three bit
     * checks OR'd together. */
    const bool present = ((perm & 0x04u) == 0x04u)
                      || ((perm & 0x08u) == 0x08u)
                      || ((perm & 0x10u) == 0x10u);
    if (present) {
        pkg.extra_data_present = true;
        pkg.extra_data = (uint8_t)((seed * 5u) & 0x0Fu);
    }

    uint8_t buf[16];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = optional_members_optional_complex_expression_container_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u perm=0x%x bytes=%zu\n", seed, perm, byte_pos);
    return 0;
}
