/* Nested compound with a variable-length array of strings sized by a
 * member of the first compound — exercises member access path. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "member_access/access_within_type/message.h"

static const char *kSentences[] = {
    "the quick brown fox",
    "jumps over",
    "the lazy dog",
    "0123456789",
    "",  /* exercise zero-length string */
    "x",
};

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    member_access_access_within_type_message_t pkg;
    member_access_access_within_type_message_init(&pkg);
    pkg.header.version = (uint16_t)(0xA000u | (seed & 0xFFFu));
    /* Pick 1..4 sentences. */
    pkg.header.num_sentences = (uint16_t)((seed % 4u) + 1u);
    /* Generator stores expression-length array count both in the parent
     * expression target and in the per-array `<name>_count` field; the
     * writer iterates the latter. Caller must keep them in sync (this
     * mirrors upstream cpp's `Vector::size() == numSentences` contract). */
    pkg.sentences_count = pkg.header.num_sentences;
    pkg.sentences = (zserio_string_t *)calloc(pkg.sentences_count, sizeof(zserio_string_t));
    if (pkg.sentences == NULL) { fprintf(stderr, "alloc\n"); return 1; }
    for (size_t i = 0; i < pkg.sentences_count; ++i) {
        const char *s = kSentences[(seed + i) % (sizeof(kSentences) / sizeof(kSentences[0]))];
        pkg.sentences[i].bytes = (char *)s;
        pkg.sentences[i].length = strlen(s);
    }

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = member_access_access_within_type_message_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); free(pkg.sentences); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u n=%u bytes=%zu\n", seed, (unsigned)pkg.header.num_sentences, byte_pos);
    free(pkg.sentences);
    return 0;
}
