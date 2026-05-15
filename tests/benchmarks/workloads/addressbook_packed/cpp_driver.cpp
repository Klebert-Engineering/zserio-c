// Verifies: REQ-PF-001
// Upstream-cpp counterpart of c_driver.c for the addressbook_packed benchmark.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <fstream>

#include "zserio/BitStreamReader.h"
#include "addressbook_packed/AddressBook.h"

#if defined(__GNUC__) || defined(__clang__)
#  define ZSERIO_BENCH_NOINLINE __attribute__((noinline))
#else
#  define ZSERIO_BENCH_NOINLINE
#endif

static ZSERIO_BENCH_NOINLINE uint64_t decode_one(const uint8_t *buf, size_t size)
{
    ::zserio::BitStreamReader r(buf, size);
    ::addressbook_packed::AddressBook book(r);
    const auto& people = book.getPeople();
    uint64_t s = static_cast<uint64_t>(people.size()) << 48;
    uint32_t id_sum = 0U;
    for (const auto& p : people) {
        id_sum += static_cast<uint32_t>(p.getId());
    }
    s |= static_cast<uint64_t>(id_sum);
    if (!people.empty()) {
        s ^= (static_cast<uint64_t>(people.back().getPhones().size()) << 32);
    }
    return s;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <n_iters> <blob.bin>\n", argv[0]);
        return 2;
    }
    const int N = std::atoi(argv[1]);
    if (N <= 0) { std::fprintf(stderr, "bad n_iters\n"); return 2; }

    std::ifstream f(argv[2], std::ios::binary);
    if (!f) { std::fprintf(stderr, "open %s\n", argv[2]); return 1; }
    std::vector<uint8_t> buf{
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>()
    };
    if (buf.empty()) { return 1; }
    const size_t bytes = buf.size();

    /* Warm-up. */
    try {
        (void)decode_one(buf.data(), buf.size());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "warm-up: %s\n", e.what()); return 1;
    }

    uint64_t sink = 0U;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        try {
            sink ^= decode_one(buf.data(), buf.size());
        } catch (const std::exception &e) {
            std::fprintf(stderr, "decode: %s\n", e.what()); return 1;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    const double decode_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
        / static_cast<double>(N);

    std::fprintf(stdout, "encode_ns_per_op=0.00 decode_ns_per_op=%.2f bytes=%zu sink=%llu\n",
                 decode_ns, bytes, static_cast<unsigned long long>(sink));
    return 0;
}
