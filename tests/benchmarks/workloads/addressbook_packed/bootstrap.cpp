// One-shot bootstrap encoder: read the upstream JSON dataset, build
// the AddressBook value via cpp reflection, write the wire-format
// BLOB to argv[2]. The harness runs this once before the timed
// decode loop; the BLOB is then the shared input for the C and cpp
// drivers (so both decode the exact same bytes).
//
// Uses upstream zserio's `fromJsonFile` + `serializeToFile`, the
// same pair their official benchmark scripts use.

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "zserio/DebugStringUtil.h"
#include "zserio/SerializeUtil.h"
#include "addressbook_packed/AddressBook.h"

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <input.json> <output.blob>\n", argv[0]);
        return 2;
    }
    try {
        auto book = ::zserio::fromJsonFile<::addressbook_packed::AddressBook>(argv[1]);
        ::zserio::serializeToFile(book, argv[2]);
        std::fprintf(stdout, "bootstrap: wrote %s\n", argv[2]);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "bootstrap: %s\n", e.what());
        return 1;
    }
    return 0;
}
