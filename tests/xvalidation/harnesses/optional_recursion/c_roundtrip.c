/* Hand-written roundtrip driver (recursive Block — c_driver builds
 * the chain manually with `b0`/`b1` rather than a single `pkg`). The
 * top-level byteCount=3 parameter matches what c_driver passes. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "optional_members/optional_recursion/block.h"

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <seed> <in.bin> <out.bin>\n", argv[0]);
        return 2;
    }
    (void)argv[1];
    const char *const in_path  = argv[2];
    const char *const out_path = argv[3];

    FILE *fi = fopen(in_path, "rb");
    if (fi == NULL) { perror(in_path); return 1; }
    fseek(fi, 0, SEEK_END);
    long fsize = ftell(fi);
    fseek(fi, 0, SEEK_SET);
    if (fsize <= 0) { fclose(fi); fprintf(stderr, "empty input\n"); return 1; }
    uint8_t *in_buf = (uint8_t *)malloc((size_t)fsize);
    fread(in_buf, 1, (size_t)fsize, fi);
    fclose(fi);

    optional_members_optional_recursion_block_t pkg;
    optional_members_optional_recursion_block_init(&pkg);
    optional_members_optional_recursion_block_set_parameters(&pkg, 3u);

    zserio_bitstream_reader_t r;
    zserio_bitstream_reader_init(&r, in_buf, (size_t)fsize * 8u);
    const zserio_allocator_t alloc = zserio_allocator_libc();
    zserio_status_t st = optional_members_optional_recursion_block_read(&pkg, &r, &alloc);
    if (st != ZSERIO_OK) { fprintf(stderr, "roundtrip: read failed: %d\n", (int)st); return 1; }

    size_t out_cap = (size_t)fsize * 4u + 4096u;
    uint8_t *out_buf = (uint8_t *)malloc(out_cap);
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, out_buf, out_cap * 8u);
    st = optional_members_optional_recursion_block_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "roundtrip: write failed: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;

    FILE *fo = fopen(out_path, "wb");
    fwrite(out_buf, 1, byte_pos, fo);
    fclose(fo);

    free(in_buf);
    free(out_buf);
    optional_members_optional_recursion_block_free(&pkg, &alloc);
    return 0;
}
