#include <cstdio>
#include <cstdlib>
#include <exception>
#include "zserio/DebugStringUtil.h"
#include "zserio/SerializeUtil.h"
#include "simpletrace_packed/SimpleTrace.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <in.json> <out.blob>\n", argv[0]); return 2; }
    try {
        auto t = ::zserio::fromJsonFile<::simpletrace_packed::SimpleTrace>(argv[1]);
        ::zserio::serializeToFile(t, argv[2]);
        std::fprintf(stdout, "bootstrap: wrote %s\n", argv[2]);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "bootstrap: %s\n", e.what()); return 1;
    }
    return 0;
}
