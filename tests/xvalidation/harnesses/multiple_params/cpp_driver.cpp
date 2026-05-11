/* See c_driver.c — parameters are not on the wire. */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "zserio/BitStreamReader.h"
#include "zserio/BitStreamWriter.h"
#include "parameterized_types/multiple_params/TestStructure.h"

int main(int argc, char *argv[])
{
    if (argc < 4) { std::fprintf(stderr, "usage: %s <in.bin> <out.bin> <seed>\n", argv[0]); return 2; }
    const unsigned seed = static_cast<unsigned>(std::atoi(argv[3]));
    const uint32_t arrayLength = static_cast<uint32_t>((seed % 3u) + 2u);
    const uint8_t bitFieldLength = static_cast<uint8_t>((seed % 7u) + 2u);

    std::FILE *fin = std::fopen(argv[1], "rb");
    std::fseek(fin, 0, SEEK_END); long n = std::ftell(fin); std::fseek(fin, 0, SEEK_SET);
    std::vector<uint8_t> in_buf(static_cast<size_t>(n));
    std::fread(in_buf.data(), 1, in_buf.size(), fin); std::fclose(fin);
    try {
        ::zserio::BitStreamReader reader(in_buf.data(), in_buf.size());
        ::parameterized_types::multiple_params::TestStructure pkg(reader, arrayLength, bitFieldLength);
        std::vector<uint8_t> out_buf(4096);
        ::zserio::BitStreamWriter writer(out_buf.data(), out_buf.size());
        pkg.write(writer);
        size_t byte_pos = (writer.getBitPosition() + 7u) / 8u;
        std::FILE *fout = std::fopen(argv[2], "wb");
        std::fwrite(out_buf.data(), 1, byte_pos, fout); std::fclose(fout);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "cpp_driver: %s\n", e.what()); return 3;
    }
    return 0;
}
