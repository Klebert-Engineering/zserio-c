// Verifies: REQ-PF-001
//
// Pmr (arena-backed) cpp counterpart of the arena c_driver. The
// `-setCppAllocator polymorphic` codegen flag (see schema.config)
// makes upstream's cpp generator emit templated structs that take a
// `zserio::pmr::PropagatingPolymorphicAllocator` — the C++ analogue
// of our bump-arena allocator. Pairing this driver with the c_driver
// gives the fair arena-vs-arena comparison the addressbook_align
// row's libc-vs-libc ratio doesn't capture.
//
// The custom `BumpMemoryResource` is a monotonic upward-bumping
// resource (no individual deallocate; the whole buffer releases on
// `reset()`) — same shape as the runtime's reference `zserio_arena_t`.

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
#include "addressbook_align/AddressBook.h"

#if defined(__GNUC__) || defined(__clang__)
#  define ZSERIO_BENCH_NOINLINE __attribute__((noinline))
#else
#  define ZSERIO_BENCH_NOINLINE
#endif

// Monotonic bump resource. doAllocate hands out aligned sub-ranges
// of a caller-owned buffer; doDeallocate is a no-op. reset() rewinds
// `used_` to 0 — matches the contract of our reference
// `zserio_arena_t` so the cpp/c ratio on this workload measures the
// generator/runtime delta with the allocator strategy held equal.
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
        if (aligned > cap_ || bytes > cap_ - aligned)
        {
            throw std::bad_alloc{};
        }
        used_ = aligned + bytes;
        if (used_ > high_water_) high_water_ = used_;
        return buf_ + aligned;
    }
    void doDeallocate(void*, std::size_t, std::size_t) override
    { /* no-op; reset() reclaims everything */ }
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
    ::addressbook_align::AddressBook book{r, alloc};
    const auto& people = book.getPeople();

    uint64_t s = static_cast<uint64_t>(people.size()) << 48;
    uint32_t id_sum = 0U;
    for (const auto& p : people)
    {
        id_sum += static_cast<uint32_t>(p.getId());
    }
    s |= static_cast<uint64_t>(id_sum);
    if (!people.empty())
    {
        s ^= (static_cast<uint64_t>(people.back().getPhones().size()) << 32);
    }
    return s;
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
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
    if (buf.empty()) return 1;

    // 4 MB pool — matches the c_driver arena size for fairness.
    constexpr std::size_t POOL_BYTES = 4U * 1024U * 1024U;
    std::vector<uint8_t> pool(POOL_BYTES);
    BumpMemoryResource arena{pool.data(), POOL_BYTES};

    try
    {
        (void)decode_one(buf.data(), buf.size(), &arena);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "warm-up: %s\n", e.what());
        return 1;
    }

    uint64_t sink = 0U;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i)
    {
        try
        {
            sink ^= decode_one(buf.data(), buf.size(), &arena);
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "decode: %s\n", e.what());
            return 1;
        }
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
