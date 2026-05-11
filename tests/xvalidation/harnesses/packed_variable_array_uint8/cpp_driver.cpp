/*
 * Cpp driver for the schema-driven cross-validation harness.
 *
 *   cpp_driver <in.bin> <out.bin>
 *
 * Reads <in.bin> (produced by c_driver), decodes via the upstream
 * generated reader, re-encodes via the upstream generated writer,
 * and writes the re-encoded bytes to <out.bin>.
 *
 * The orchestrator (run_xvalidation.sh) then `cmp`s the two files —
 * byte-equal means upstream agrees with our wire-format on this value.
 */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "zserio/BitStreamReader.h"
#include "zserio/BitStreamWriter.h"
#include "array_types/packed_variable_array_uint8/PackedVariableArray.h"

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <in.bin> <out.bin>\n", argv[0]);
        return 2;
    }
    const char *in_path = argv[1];
    const char *out_path = argv[2];

    std::FILE *fin = std::fopen(in_path, "rb");
    if (!fin) { std::perror("fopen in"); return 1; }
    std::fseek(fin, 0, SEEK_END);
    long n = std::ftell(fin);
    std::fseek(fin, 0, SEEK_SET);
    std::vector<uint8_t> in_buf(static_cast<size_t>(n));
    if (std::fread(in_buf.data(), 1, in_buf.size(), fin) != in_buf.size()) {
        std::perror("fread"); return 1;
    }
    std::fclose(fin);

    try {
        ::zserio::BitStreamReader reader(in_buf.data(), in_buf.size());
        ::array_types::packed_variable_array_uint8::PackedVariableArray pkg(reader);

        // Re-encode. A 4 KB scratch buffer is way more than count<=32
        // packed uint8 needs.
        std::vector<uint8_t> out_buf(4096);
        ::zserio::BitStreamWriter writer(out_buf.data(), out_buf.size());
        pkg.write(writer);
        const size_t bit_pos = writer.getBitPosition();
        const size_t byte_pos = (bit_pos + 7u) / 8u;

        std::FILE *fout = std::fopen(out_path, "wb");
        if (!fout) { std::perror("fopen out"); return 1; }
        if (std::fwrite(out_buf.data(), 1, byte_pos, fout) != byte_pos) {
            std::perror("fwrite"); return 1;
        }
        std::fclose(fout);
        std::fprintf(stdout, "cpp_driver: in_bytes=%ld out_bytes=%zu numElements=%u\n",
                     n, byte_pos, static_cast<unsigned>(pkg.getNumElements()));
    } catch (const std::exception &e) {
        std::fprintf(stderr, "cpp_driver: decode/encode failed: %s\n", e.what());
        return 3;
    }
    return 0;
}
