/* Conditional optional + parameterized struct gated by the same
 * condition: when basicColor == BLACK, both numBlackTones and
 * blackColor(numBlackTones) are present. */

#include <stdio.h>
#include <stdlib.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "optional_members/optional_expression/container.h"
#include "optional_members/optional_expression/black_color.h"
#include "optional_members/optional_expression/basic_color.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    optional_members_optional_expression_container_t pkg;
    optional_members_optional_expression_container_init(&pkg);

    /* BLACK=0, WHITE=1. */
    pkg.basic_color = (seed & 1u) ? 0u : 1u;
    if (pkg.basic_color == 0u) {  /* BLACK → both optionals present */
        pkg.num_black_tones_present = true;
        pkg.num_black_tones = (uint8_t)((seed % 3u) + 1u);
        pkg.black_color_present = true;
        optional_members_optional_expression_black_color_set_parameters(
                &pkg.black_color, pkg.num_black_tones);
        pkg.black_color.tones_count = pkg.num_black_tones;
        pkg.black_color.tones = (int32_t *)calloc(pkg.num_black_tones, sizeof(int32_t));
        for (size_t i = 0; i < pkg.num_black_tones; ++i)
            pkg.black_color.tones[i] = (int32_t)(seed * 1103u + (unsigned)i);
    }

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = optional_members_optional_expression_container_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
