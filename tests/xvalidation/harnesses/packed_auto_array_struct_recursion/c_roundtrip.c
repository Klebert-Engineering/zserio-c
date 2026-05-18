/* Hand-written roundtrip driver (recursive struct — c_driver uses
 * `root` rather than `pkg`, so the generator skips it). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "array_types/packed_auto_array_struct_recursion/packed_auto_array_recursion.h"

typedef array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_t Node;

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

    Node pkg;
    array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_init(&pkg);

    zserio_bitstream_reader_t r;
    zserio_bitstream_reader_init(&r, in_buf, (size_t)fsize * 8u);
    const zserio_allocator_t alloc = zserio_allocator_libc();
    zserio_status_t st = array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_read(&pkg, &r, &alloc);
    if (st != ZSERIO_OK) { fprintf(stderr, "roundtrip: read failed: %d\n", (int)st); return 1; }

    size_t out_cap = (size_t)fsize * 4u + 4096u;
    uint8_t *out_buf = (uint8_t *)malloc(out_cap);
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, out_buf, out_cap * 8u);
    st = array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "roundtrip: write failed: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;

    FILE *fo = fopen(out_path, "wb");
    fwrite(out_buf, 1, byte_pos, fo);
    fclose(fo);

    free(in_buf);
    free(out_buf);
    array_types_packed_auto_array_struct_recursion_packed_auto_array_recursion_free(&pkg, &alloc);
    return 0;
}
