#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "zserio/BitStreamReader.h"
#include "zserio/BitStreamWriter.h"
#include "templates/instantiate_type_as_parameter/InstantiateTypeAsParameter.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <in.bin> <out.bin>\n", argv[0]); return 2; }
    std::FILE *fin = std::fopen(argv[1], "rb");
    std::fseek(fin, 0, SEEK_END); long n = std::ftell(fin); std::fseek(fin, 0, SEEK_SET);
    std::vector<uint8_t> in_buf(static_cast<size_t>(n));
    std::fread(in_buf.data(), 1, in_buf.size(), fin); std::fclose(fin);
    try {
        ::zserio::BitStreamReader reader(in_buf.data(), in_buf.size());
        ::templates::instantiate_type_as_parameter::InstantiateTypeAsParameter pkg(reader);
        std::vector<uint8_t> out_buf(128);
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
