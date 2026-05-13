package benchmarks.medium_nested;

// REQ-PF-001 medium-nested workload. Two levels of nesting: an
// outer MediumNested holding 10 Inner structs, each with the same
// 10-field shape as the small_flat workload. Total ~100 leaf
// fields, encoded size 10 × ~31.5 = ~315 bytes.
struct Inner
{
    uint8  a;
    uint16 b;
    uint32 c;
    uint64 d;
    int8   e;
    int16  f;
    int32  g;
    int64  h;
    bool   i;
    bit:4  j;
};

struct MediumNested
{
    Inner a;
    Inner b;
    Inner c;
    Inner d;
    Inner e;
    Inner f;
    Inner g;
    Inner h;
    Inner i;
    Inner j;
};
