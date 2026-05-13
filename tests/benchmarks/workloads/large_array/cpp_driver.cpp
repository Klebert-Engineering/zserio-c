// Verifies: REQ-PF-001
// Upstream-cpp counterpart of c_driver.c for the LargeArray workload.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>

#include "zserio/BitStreamReader.h"
#include "zserio/BitStreamWriter.h"
#include "benchmarks/large_array/LargeArray.h"

static constexpr size_t ELEMENT_COUNT = 262144U;
static constexpr size_t BUF_BYTES = ELEMENT_COUNT * 4U + 16U;

int main(int argc, char *argv[])
{
    const int N = (argc > 1) ? std::atoi(argv[1]) : 200;
    if (N <= 0) { std::fprintf(stderr, "usage: %s [n_iters]\n", argv[0]); return 2; }

    ::benchmarks::large_array::LargeArray v;
    auto& data = v.getData();
    data.reserve(ELEMENT_COUNT);
    uint32_t s = 0xCAFEBABEU;
    for (size_t i = 0U; i < ELEMENT_COUNT; ++i) {
        s = s * 1103515245U + 12345U;
        data.push_back(s);
    }

    std::vector<uint8_t> buf(BUF_BYTES);
    ::zserio::BitStreamWriter w_warmup(buf.data(), buf.size());
    v.write(w_warmup);
    const size_t bits  = w_warmup.getBitPosition();
    const size_t bytes = (bits + 7U) / 8U;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        ::zserio::BitStreamWriter w(buf.data(), buf.size());
        v.write(w);
    }
    auto t1 = std::chrono::steady_clock::now();
    const double encode_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
        / static_cast<double>(N);

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        ::zserio::BitStreamReader r(buf.data(), buf.size());
        ::benchmarks::large_array::LargeArray out(r);
        (void)out;
    }
    t1 = std::chrono::steady_clock::now();
    const double decode_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
        / static_cast<double>(N);

    std::fprintf(stdout, "encode_ns_per_op=%.2f decode_ns_per_op=%.2f bytes=%zu\n",
                 encode_ns, decode_ns, bytes);
    return 0;
}
