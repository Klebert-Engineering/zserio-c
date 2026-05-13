// Verifies: REQ-PF-001
//
// Upstream-cpp counterpart of c_driver.c. Identical workload, same
// inner-loop shape. The harness runs both and computes the ratio.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>

#include "zserio/BitStreamReader.h"
#include "zserio/BitStreamWriter.h"
#include "benchmarks/small_flat/SmallFlat.h"

int main(int argc, char *argv[])
{
    const int N = (argc > 1) ? std::atoi(argv[1]) : 200000;
    if (N <= 0) { std::fprintf(stderr, "usage: %s [n_iters]\n", argv[0]); return 2; }

    ::benchmarks::small_flat::SmallFlat v;
    v.setA(0xABU);
    v.setB(0x1234U);
    v.setC(0x12345678U);
    v.setD(UINT64_C(0x0123456789ABCDEF));
    v.setE(-42);
    v.setF(-12345);
    v.setG(-1234567);
    v.setH(INT64_C(-1234567890123));
    v.setI(true);
    v.setJ(0x0BU);

    std::vector<uint8_t> buf(64);
    /* Warm-up + size discovery. */
    ::zserio::BitStreamWriter w_warmup(buf.data(), buf.size());
    v.write(w_warmup);
    const size_t bits  = w_warmup.getBitPosition();
    const size_t bytes = (bits + 7U) / 8U;

    /* Encode timing. */
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        ::zserio::BitStreamWriter w(buf.data(), buf.size());
        v.write(w);
    }
    auto t1 = std::chrono::steady_clock::now();
    const double encode_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
        / static_cast<double>(N);

    /* Decode timing. */
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        ::zserio::BitStreamReader r(buf.data(), buf.size());
        ::benchmarks::small_flat::SmallFlat out(r);
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
