// Verifies: REQ-PF-001
//
// Upstream-cpp counterpart of c_driver.c. Same workload shape, same
// inner-loop count.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>

#include "zserio/BitStreamReader.h"
#include "zserio/BitStreamWriter.h"
#include "benchmarks/medium_nested/MediumNested.h"

static void fill_inner(::benchmarks::medium_nested::Inner& v, uint32_t k)
{
    v.setA(static_cast<uint8_t>(0xA0U + k));
    v.setB(static_cast<uint16_t>(0x1230U + k));
    v.setC(0x12345670U + k);
    v.setD(UINT64_C(0x0123456789ABCDE0) + static_cast<uint64_t>(k));
    v.setE(static_cast<int8_t>(-42 - static_cast<int8_t>(k)));
    v.setF(static_cast<int16_t>(-12000 - static_cast<int16_t>(k)));
    v.setG(static_cast<int32_t>(-1234560 - static_cast<int32_t>(k)));
    v.setH(INT64_C(-1234567890120) - static_cast<int64_t>(k));
    v.setI((k & 1U) != 0U);
    v.setJ(static_cast<uint8_t>(k & 0x0FU));
}

int main(int argc, char *argv[])
{
    const int N = (argc > 1) ? std::atoi(argv[1]) : 50000;
    if (N <= 0) { std::fprintf(stderr, "usage: %s [n_iters]\n", argv[0]); return 2; }

    ::benchmarks::medium_nested::MediumNested v;
    fill_inner(v.getA(), 0U);
    fill_inner(v.getB(), 1U);
    fill_inner(v.getC(), 2U);
    fill_inner(v.getD(), 3U);
    fill_inner(v.getE(), 4U);
    fill_inner(v.getF(), 5U);
    fill_inner(v.getG(), 6U);
    fill_inner(v.getH(), 7U);
    fill_inner(v.getI(), 8U);
    fill_inner(v.getJ(), 9U);

    std::vector<uint8_t> buf(512);
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
        ::benchmarks::medium_nested::MediumNested out(r);
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
