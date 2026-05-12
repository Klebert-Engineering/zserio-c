#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "zserio/BitStreamReader.h"
#include "zserio/BitStreamWriter.h"
#include "choice_types/uint64_param_choice/UInt64ParamChoice.h"

static uint64_t selector_for(unsigned seed)
{
    static const uint64_t table[] = {1u, 3u, 5u, 0xFFFFFFFFu, 17u};
    return table[seed % (sizeof(table) / sizeof(table[0]))];
}

int main(int argc, char *argv[])
{
    if (argc < 4) { std::fprintf(stderr, "usage: %s <in.bin> <out.bin> <seed>\n", argv[0]); return 2; }
    const unsigned seed = static_cast<unsigned>(std::atoi(argv[3]));
    const uint64_t sel = selector_for(seed);

    std::FILE *fin = std::fopen(argv[1], "rb");
    std::fseek(fin, 0, SEEK_END); long n = std::ftell(fin); std::fseek(fin, 0, SEEK_SET);
    std::vector<uint8_t> in_buf(static_cast<size_t>(n));
    std::fread(in_buf.data(), 1, in_buf.size(), fin); std::fclose(fin);
    try {
        ::zserio::BitStreamReader reader(in_buf.data(), in_buf.size());
        ::choice_types::uint64_param_choice::UInt64ParamChoice pkg(reader, sel);
        std::vector<uint8_t> out_buf(32);
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
