// Verifies: REQ-PF-001
//
// Pmr (arena-backed) cpp counterpart of the arena c_driver. Pairs
// with `addressbook_align_arena/cpp_driver.cpp` (same
// `BumpMemoryResource` pattern, same `PropagatingPolymorphicAllocator`
// shape) on a packed-delta payload rather than a string-heavy one.
// See docs/benchmarks/methodology.md § Allocator choice.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <new>
#include <vector>

#include "zserio/BitStreamReader.h"
#include "zserio/pmr/MemoryResource.h"
#include "zserio/pmr/PolymorphicAllocator.h"
#include "simpletrace_packed/SimpleTrace.h"

#if defined(__GNUC__) || defined(__clang__)
#  define ZSERIO_BENCH_NOINLINE __attribute__((noinline))
#else
#  define ZSERIO_BENCH_NOINLINE
#endif

class BumpMemoryResource final : public ::zserio::pmr::MemoryResource
{
public:
    BumpMemoryResource(uint8_t* buf, std::size_t cap) noexcept
        : buf_(buf), cap_(cap), used_(0), high_water_(0)
    {}
    void reset() noexcept { used_ = 0; }
    std::size_t high_water() const noexcept { return high_water_; }

private:
    void* doAllocate(std::size_t bytes, std::size_t alignment) override
    {
        const std::size_t mask = alignment - 1U;
        const std::size_t aligned = (used_ + mask) & ~mask;
        if (aligned > cap_ || bytes > cap_ - aligned) { throw std::bad_alloc{}; }
        used_ = aligned + bytes;
        if (used_ > high_water_) high_water_ = used_;
        return buf_ + aligned;
    }
    void doDeallocate(void*, std::size_t, std::size_t) override { /* no-op */ }
    bool doIsEqual(const ::zserio::pmr::MemoryResource& other) const noexcept override
    {
        return &other == this;
    }

    uint8_t*    buf_;
    std::size_t cap_;
    std::size_t used_;
    std::size_t high_water_;
};

static ZSERIO_BENCH_NOINLINE uint64_t decode_one(const uint8_t* buf, std::size_t size,
                                                 BumpMemoryResource* arena)
{
    arena->reset();
    ::zserio::pmr::PropagatingPolymorphicAllocator<uint8_t> alloc{arena};

    ::zserio::BitStreamReader r{buf, size};
    ::simpletrace_packed::SimpleTrace t{r, alloc};
    const auto& trace = t.getTrace();
    uint64_t s = static_cast<uint64_t>(trace.size()) << 32;
    int64_t lon_xor = 0;
    for (const auto& p : trace) { lon_xor ^= p.getLongitude(); }
    s ^= static_cast<uint64_t>(lon_xor);
    return s;
}

int main(int argc, char* argv[])
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <n> <blob>\n", argv[0]); return 2; }
    const int N = std::atoi(argv[1]);
    if (N <= 0) return 2;

    std::ifstream f(argv[2], std::ios::binary);
    if (!f) return 1;
    std::vector<uint8_t> buf{ std::istreambuf_iterator<char>(f),
                              std::istreambuf_iterator<char>() };
    if (buf.empty()) return 1;

    constexpr std::size_t POOL_BYTES = 1U * 1024U * 1024U;
    std::vector<uint8_t> pool(POOL_BYTES);
    BumpMemoryResource arena{pool.data(), POOL_BYTES};

    try { (void)decode_one(buf.data(), buf.size(), &arena); }
    catch (const std::exception &e) { std::fprintf(stderr, "warmup: %s\n", e.what()); return 1; }

    uint64_t sink = 0U;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        try { sink ^= decode_one(buf.data(), buf.size(), &arena); }
        catch (const std::exception &e) { std::fprintf(stderr, "decode: %s\n", e.what()); return 1; }
    }
    auto t1 = std::chrono::steady_clock::now();
    const double decode_ns =
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
        / static_cast<double>(N);

    std::fprintf(stdout,
                 "encode_ns_per_op=0.00 decode_ns_per_op=%.2f bytes=%zu sink=%llu arena_high_water=%zu\n",
                 decode_ns, buf.size(), static_cast<unsigned long long>(sink),
                 arena.high_water());
    return 0;
}
