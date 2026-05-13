package benchmarks.large_array;

// REQ-PF-001 large-array workload. Auto-array of uint32 elements;
// 262144 elements × 4 bytes ≈ 1 MB encoded plus a small varsize
// length prefix. This stresses the bulk-byte path of the bitstream
// runtime — the per-call overhead vanishes against the per-byte
// work, so the comparison vs upstream reflects steady-state
// throughput rather than per-message latency.
struct LargeArray
{
    uint32 data[];
};
