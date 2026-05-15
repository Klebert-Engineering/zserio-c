#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <fstream>

#include "zserio/BitStreamReader.h"
#include "carsales_align/CarSales.h"

#if defined(__GNUC__) || defined(__clang__)
#  define ZSERIO_BENCH_NOINLINE __attribute__((noinline))
#else
#  define ZSERIO_BENCH_NOINLINE
#endif

static ZSERIO_BENCH_NOINLINE uint64_t decode_one(const uint8_t *buf, size_t size)
{
    ::zserio::BitStreamReader r(buf, size);
    ::carsales_align::CarSales v(r);
    uint64_t s = static_cast<uint64_t>(v.getNumSoldCars()) << 48;
    uint64_t price_sum = 0U;
    for (const auto& cs : v.getCarSales()) {
        price_sum += static_cast<uint64_t>(cs.getPrice());
    }
    s ^= price_sum;
    return s;
}

int main(int argc, char *argv[])
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <n> <blob>\n", argv[0]); return 2; }
    const int N = std::atoi(argv[1]);
    if (N <= 0) return 2;

    std::ifstream f(argv[2], std::ios::binary);
    if (!f) return 1;
    std::vector<uint8_t> buf{ std::istreambuf_iterator<char>(f),
                              std::istreambuf_iterator<char>() };
    if (buf.empty()) return 1;
    const size_t bytes = buf.size();

    try { (void)decode_one(buf.data(), buf.size()); }
    catch (const std::exception &e) { std::fprintf(stderr, "warmup: %s\n", e.what()); return 1; }

    uint64_t sink = 0U;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        try { sink ^= decode_one(buf.data(), buf.size()); }
        catch (const std::exception &e) { std::fprintf(stderr, "decode: %s\n", e.what()); return 1; }
    }
    auto t1 = std::chrono::steady_clock::now();
    const double decode_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
        / static_cast<double>(N);

    std::fprintf(stdout, "encode_ns_per_op=0.00 decode_ns_per_op=%.2f bytes=%zu sink=%llu\n",
                 decode_ns, bytes, static_cast<unsigned long long>(sink));
    return 0;
}
