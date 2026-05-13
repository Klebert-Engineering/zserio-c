package benchmarks.small_flat;

// REQ-PF-001 small flat-struct workload. Ten fixed-width primitive
// fields covering the full integer width set plus bool. Encoded size:
// 1 + 2 + 4 + 8 + 1 + 2 + 4 + 8 + 1 + 0.5 = ~31.5 bytes per value.
struct SmallFlat
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
