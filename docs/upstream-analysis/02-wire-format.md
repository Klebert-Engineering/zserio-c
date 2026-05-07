# 02 · Wire format — bit layout per construct

**Purpose.** This document specifies the bit-level encoding for every
zserio construct in v1 scope (per `01-schema-language.md`), with
upstream citations. The intent is that someone writing the Phase 1
C99 runtime can implement encoder and decoder from this document
alone, with the upstream code as a cross-check.

**Authoritative sources** (at pinned tag `v2.18.1`):
- `third_party/zserio/doc/ZserioEncodingGuide.md` (Encoding Guide 1.0)
- `third_party/zserio/compiler/extensions/cpp/runtime/src/zserio/BitStreamReader.{h,cpp}`
- `third_party/zserio/compiler/extensions/cpp/runtime/src/zserio/BitStreamWriter.{h,cpp}`
- `third_party/zserio/compiler/extensions/cpp/runtime/src/zserio/FloatUtil.{h,cpp}`
- `third_party/zserio/compiler/extensions/cpp/runtime/src/zserio/BitFieldUtil.{h,cpp}`
- `third_party/zserio/compiler/extensions/cpp/runtime/src/zserio/DeltaContext.h`

When this document and the Encoding Guide disagree, the Encoding Guide
wins and this document is wrong — file an issue.

## 1. Conventions

These rules apply globally. Each section below assumes them and only
restates them where there is a non-obvious interaction.

- **Byte order**: big-endian. Multi-byte integers store the most
  significant byte first.
- **Bit order within a byte**: most significant bit first. The first
  bit emitted to the stream goes into bit 7 of the first byte, then
  bit 6, etc.
- **Signed integers**: two's complement.
- **Bit positions are continuous**: there is no implicit padding
  between fields. A `bit:4` followed by a `bit:3` produces 7 bits with
  no gap. Padding only happens where the schema demands it
  (alignment, offsets, extends).
- **Allocation-relevant prefixes**: `string`, `bytes`, `extern`, and
  auto-arrays carry a `varsize` length prefix at the current bit
  position. These prefixes are NOT byte-aligned by themselves; they
  start wherever the bit cursor is.
- **No tags by default**: structs, choices, fixed arrays carry no
  type or length tag; layout is determined entirely by the schema.
  Unions and auto-arrays are the exceptions and explicitly carry tags
  or lengths.

## 2. Primitive types

### 2.1 Fixed-width integers

`uint8`, `uint16`, `uint32`, `uint64`, `int8`, `int16`, `int32`, `int64`.

**Layout**: exactly the bit width of the type, big-endian, MSB first.
Signed values use two's complement.

| Type     | Bits | Encoded form             |
|----------|-----:|--------------------------|
| `uint8`  |    8 | 8-bit unsigned, BE       |
| `uint16` |   16 | 16-bit unsigned, BE      |
| `uint32` |   32 | 32-bit unsigned, BE      |
| `uint64` |   64 | 64-bit unsigned, BE      |
| `int8`   |    8 | 8-bit two's complement   |
| `int16`  |   16 | 16-bit two's complement  |
| `int32`  |   32 | 32-bit two's complement  |
| `int64`  |   64 | 64-bit two's complement  |

These types do NOT auto-align: when embedded inside a struct that
contains bit fields, they begin at the current bit position regardless
of byte boundary.

**Upstream**: `BitStreamReader::readBits`, `BitStreamReader::readSignedBits`,
`BitStreamWriter::writeBits`, `BitStreamWriter::writeSignedBits`
(generic over width).

**Runtime API sketch**:

```c
zserio_status_t zserio_read_uint8 (zserio_bitstream_t *bs, uint8_t  *out);
zserio_status_t zserio_read_uint16(zserio_bitstream_t *bs, uint16_t *out);
zserio_status_t zserio_read_uint32(zserio_bitstream_t *bs, uint32_t *out);
zserio_status_t zserio_read_uint64(zserio_bitstream_t *bs, uint64_t *out);
zserio_status_t zserio_read_int8  (zserio_bitstream_t *bs, int8_t   *out);
/* … int16/32/64 mirror … */
zserio_status_t zserio_write_uint8 (zserio_bitstream_t *bs, uint8_t  v);
/* … */
```

The widths are fixed enough that we generate them as separate functions
rather than going through a generic `read_bits(width)` for every primitive
— inlining and dead-code elimination work better, and the names are
self-documenting in generated code.

### 2.2 Bit fields

`bit:N`, `int:N`, `bit<expr>`, `int<expr>` for `N` ∈ [1, 64].

**Layout**: exactly `N` bits, big-endian, MSB first. Signed bit fields
are two's complement in `N` bits, sign-extended on read.

The width may be a compile-time constant (`bit:5`) or a runtime
expression evaluated at the field's encode/decode site (`bit<expr>`).

**Bounds** for `int:N` and `bit:N` are enumerated by
`BitFieldUtil::getBitFieldLowerBound` / `getBitFieldUpperBound`:
- `bit:N`  ∈ [0, 2^N − 1]
- `int:N`  ∈ [−2^(N−1), 2^(N−1) − 1]

`N = 0` is forbidden by the language. `N > 64` is forbidden (no 128-bit
primitive). The dynamic-width forms must compute a value in [1, 64] at
the call site; runtime returns `ZSERIO_ERR_INVALID_ARGUMENT` on
out-of-range width.

**Runtime API**:

```c
zserio_status_t zserio_read_bits  (zserio_bitstream_t *bs, uint8_t numBits, uint64_t *out);
zserio_status_t zserio_read_signed(zserio_bitstream_t *bs, uint8_t numBits, int64_t  *out);
zserio_status_t zserio_write_bits  (zserio_bitstream_t *bs, uint8_t numBits, uint64_t v);
zserio_status_t zserio_write_signed(zserio_bitstream_t *bs, uint8_t numBits, int64_t  v);
```

`numBits` is a `uint8_t` to make the [1, 64] domain explicit and cheap
to validate at the boundary (REQ-FS-006).

### 2.3 Variable-length integers

| Type        | Max bytes | First-byte layout             | Continuation bytes |
|-------------|----------:|-------------------------------|--------------------|
| `varint16`  |         2 | sign(1) + cont(1) + value(6)  | 8 bits value       |
| `varuint16` |         2 | cont(1) + value(7)            | 8 bits value       |
| `varint32`  |         4 | sign(1) + cont(1) + value(6)  | cont(1) + value(7) for bytes 1..2; 8 bits value on byte 3 |
| `varuint32` |         4 | cont(1) + value(7)            | cont(1) + value(7) for bytes 1..2; 8 bits value on byte 3 |
| `varint64`  |         8 | sign(1) + cont(1) + value(6)  | cont(1) + value(7) for bytes 1..6; 8 bits value on byte 7 |
| `varuint64` |         8 | cont(1) + value(7)            | cont(1) + value(7) for bytes 1..6; 8 bits value on byte 7 |
| `varint`    |         9 | sign(1) + cont(1) + value(6)  | cont(1) + value(7) for bytes 1..7; 8 bits value on byte 8 |
| `varuint`   |         9 | cont(1) + value(7)            | cont(1) + value(7) for bytes 1..7; 8 bits value on byte 8 |
| `varsize`   |         5 | cont(1) + value(2..7)*        | cont(1) + value(7) for bytes 1..3; 8 bits value on byte 4 |

`*` First byte of `varsize` carries only **2** value bits in the
maximum case; this is what limits `varsize` to `2^31 − 1`. For values
that fit in fewer bytes, the first byte carries up to 7 value bits.
Effectively: `varsize` is a `varuint32` capped at 31 value bits.

**Sign bit** in signed varints is the most-significant bit of the first
byte (bit 7), separate from the magnitude. The magnitude bits across
bytes are read MSB-first as a single integer; the sign is applied last.
Negative zero is not encoded — magnitude `0` with sign bit `1` is
illegal input, runtime returns `ZSERIO_ERR_INVALID_ENCODING`.

**Continuation bit** (`cont`) on each byte except the last in the type's
maximum-length form. When `cont == 0`, the integer ends. The last byte
of every type has all 8 bits as value (no `cont` slot) — the type's
maximum length is intrinsic to the type.

**Bound checking on decode**: a value that needs more bytes than the
type allows is malformed — runtime returns `ZSERIO_ERR_INVALID_ENCODING`
without consuming further bits. (Matches REQ-FS-005: bounded.)

**Upstream**: `BitStreamReader::readVarUInt32` / `readVarInt32` etc.,
plus their `Writer` counterparts. Encoding is exact-match — no leniency
on canonical form; the encoder must use the shortest representation
that fits the value.

**Runtime API**:

```c
zserio_status_t zserio_read_varuint16(zserio_bitstream_t *bs, uint16_t *out);
zserio_status_t zserio_read_varuint32(zserio_bitstream_t *bs, uint32_t *out);
zserio_status_t zserio_read_varuint64(zserio_bitstream_t *bs, uint64_t *out);
zserio_status_t zserio_read_varuint  (zserio_bitstream_t *bs, uint64_t *out); /* up to 9 bytes */
zserio_status_t zserio_read_varsize  (zserio_bitstream_t *bs, uint32_t *out);
/* … signed mirrors with int*_t outputs … */
/* … writers symmetric … */
```

### 2.4 Floats

`float16`, `float32`, `float64`. IEEE 754 half / single / double bit
patterns. The bit pattern is encoded as a 16- / 32- / 64-bit unsigned
integer using the integer rules (BE, MSB first).

**Pack/unpack** is integer-only on the bit pattern, satisfying REQ-FS-004
(no FP in control flow). Upstream provides:

- `convertFloatToUInt16(float)` / `convertUInt16ToFloat(uint16_t)`
- `convertFloatToUInt32(float)` / `convertUInt32ToFloat(uint32_t)`
- `convertDoubleToUInt64(double)` / `convertUInt64ToDouble(uint64_t)`

For float16, the `convertFloatToUInt16` performs the half-precision
narrowing arithmetic on the bit pattern explicitly (no native `_Float16`
required). For float32/float64, the conversion is just a `memcpy` (or
type-punning via union, since the host's `float`/`double` are assumed
IEEE 754 — verify in CI on each target).

**Runtime API**:

```c
zserio_status_t zserio_read_float16 (zserio_bitstream_t *bs, float  *out);
zserio_status_t zserio_read_float32 (zserio_bitstream_t *bs, float  *out);
zserio_status_t zserio_read_float64 (zserio_bitstream_t *bs, double *out);
/* … writers … */
```

The runtime always returns the host-native float; per REQ-FS-004, control
flow does not branch on the value, only the bit pattern.

### 2.5 Boolean

One bit. `1` is `true`, `0` is `false`. No alignment.

```c
zserio_status_t zserio_read_bool (zserio_bitstream_t *bs, bool *out);
zserio_status_t zserio_write_bool(zserio_bitstream_t *bs, bool v);
```

### 2.6 String

`varsize` byte length followed by that many UTF-8-encoded bytes. The
byte payload starts at the current bit position (NOT byte-aligned).

**Validation** (REQ-FS-006): the decoder validates that the byte length
fits within the remaining buffer. UTF-8 well-formedness is **not**
validated by upstream and is therefore not validated by us either, for
wire-compat (REQ-FN-001). Callers concerned about malformed UTF-8 must
validate after decode.

**Upstream**: `BitStreamReader::readString`,
`BitStreamWriter::writeString`. The upstream returns `std::string`
(allocates through allocator); the C API mirrors with caller-supplied
allocator and returns `{const char *bytes, size_t length}` (length in
bytes, NOT code points; not NUL-terminated).

**Runtime API**:

```c
typedef struct {
    const char *bytes;     /* UTF-8, NOT NUL-terminated */
    size_t      length;    /* bytes */
} zserio_string_view_t;

typedef struct {
    char  *bytes;          /* allocator-owned */
    size_t length;
} zserio_string_t;

zserio_status_t zserio_read_string (zserio_bitstream_t *bs,
                                    const zserio_allocator_t *alloc,
                                    zserio_string_t *out);
zserio_status_t zserio_read_string_view (zserio_bitstream_t *bs,
                                         zserio_string_view_t *out);
zserio_status_t zserio_write_string(zserio_bitstream_t *bs,
                                    zserio_string_view_t s);
```

The view variant is zero-copy (references the source buffer; valid only
while the buffer lives) and has the same shape as the extern/bytes view
APIs from `01-schema-language.md` §6.4.

### 2.7 Extern

`varsize` **bit** length followed by that many raw bits at the current
bit position. The bit payload is NOT byte-aligned at start, and the
last byte may have unused trailing bits.

**Runtime API** (per `01-schema-language.md` §6.4):

```c
typedef struct {
    const uint8_t *base;
    size_t         bit_offset;     /* 0..7 within base[0] */
    size_t         bit_length;     /* total bits in payload */
} zserio_bit_view_t;

zserio_status_t zserio_read_extern_view (zserio_bitstream_t *bs,
                                         zserio_bit_view_t *out);
zserio_status_t zserio_read_extern_into (zserio_bitstream_t *bs,
                                         const zserio_allocator_t *alloc,
                                         zserio_bit_view_t *owned_out);
zserio_status_t zserio_write_extern     (zserio_bitstream_t *bs,
                                         zserio_bit_view_t v);
```

**Upstream**: `BitStreamReader::readBitBuffer`, `BitStreamWriter::writeBitBuffer`.

### 2.8 Bytes

`varsize` **byte** length followed by that many raw bytes at the current
bit position. Distinguished from `extern` by length unit: bytes counts
bytes, extern counts bits. When `bytes` payload is bit-misaligned at
start (because preceding fields didn't end on a byte boundary), the
runtime still places it bit-by-bit — `bytes` does NOT auto-align.

**Runtime API**:

```c
typedef struct {
    const uint8_t *base;
    size_t         length;     /* bytes */
} zserio_bytes_view_t;
/* read_bytes_view / read_bytes_into / write_bytes mirroring extern */
```

> Implementation note: when the bytes payload is byte-aligned at start
> AND the caller wants a byte-pointer view, the runtime can produce a
> direct pointer into the source. Otherwise the materializing read is
> required to produce contiguous bytes. The borrowed-view API for
> `bytes` therefore yields a `bytes_view` only when alignment permits;
> in the misaligned case it falls back to materialize-or-fail. This
> contract differs slightly from `extern`, where the bit-view always
> succeeds because it natively expresses bit-offset.

## 3. User-defined building blocks

### 3.1 Enum

Encoded as the underlying type. An enum declared `enum bit:3 Color {…}`
encodes each value as a 3-bit unsigned integer; `enum varsize Tag {…}`
encodes as varsize.

Validation on decode: the integer value must match a declared enumerator.
If not, runtime returns `ZSERIO_ERR_INVALID_ENUM` and outputs the raw
integer alongside (so the caller can log it). Upstream's behavior is
the same — see `BitStreamReader::readVarSize` callers in generated code.

### 3.2 Bitmask

Encoded as the underlying type. No validation that the bitmask integer
contains only declared flag combinations — bitmask semantics allow OR
of any flag subset, and zserio does not pin the closed set.

### 3.3 Struct

Field-by-field, in declaration order, with **no padding or alignment
between fields by default**. A struct that contains a `bit:4` followed
by a `uint8` produces 12 bits, with the `uint8` straddling a byte
boundary (Encoding Guide §Structure Types example).

Padding is introduced only by:
- `align(N)` directives on a field (§6.1).
- `offset:` field bindings (§6.2).
- `extend` keyword on a field (§6.4 — byte-align before the extend).

**Encoder's job**:
1. For each field in order, write its bits.
2. For fields preceded by `align(N)` or pinned to an `offset:` field,
   write zero bits to align before the field's bits.
3. For fields written for an `offset:` binding, the encoder backfills
   the offset value into the offset field after the dependent field's
   position is known.

### 3.4 Choice

Encoded as the **selected branch only**. There is NO tag on the wire.
The selector is a parameter passed in from the enclosing struct or by
the caller — the parameter itself is encoded by the enclosing struct,
not by the choice.

Decoding requires the caller to supply the selector value. The runtime's
choice decode functions take the parameter as an argument; per-tag dispatch
is done at compile time in generated code via `switch`.

### 3.5 Union

Encoded as a `varsize` choice tag (zero-based index of the selected
branch) followed by the field data of that branch. Unlike `choice`, the
selector is on the wire; the receiver does not need any out-of-band
information to decode.

Generated C representation:

```c
typedef enum simple_union_tag {
    SIMPLE_UNION_TAG_VALUE8  = 0,
    SIMPLE_UNION_TAG_VALUE16 = 1,
} simple_union_tag_t;

typedef struct simple_union {
    simple_union_tag_t tag;
    union {
        uint8_t  value8;
        uint16_t value16;
    } body;
} simple_union_t;
```

Decode reads the tag, switches on it, decodes the body, returns
`ZSERIO_ERR_INVALID_TAG` if the tag is out of range.

## 4. Field-level features

### 4.1 Optional

Two forms:

**Auto-optional (`optional` keyword)** — encoded as a single presence
bit followed by the value when present, or just the presence bit when
absent.

| Bits | Description       |
|------|-------------------|
| 0    | presence flag     |
| 1..  | value (if present)|

**If-clause optional (`field T x if cond;`)** — no presence bit on the
wire. The condition is evaluated from already-decoded state; if true,
the field is encoded; if false, nothing is. Decode must evaluate the
same condition.

The two forms are equivalent in expressive power: the auto-optional is
syntactic sugar for an if-clause where the condition reads a synthetic
boolean field. Upstream emits both forms identically apart from the
single-bit prefix.

Generated C: optional fields get a parallel `bool _present_<field>`
per `01-schema-language.md` §5. No runtime "optional holder" type.

### 4.2 Constraints

Encoded the same as the field they decorate. Constraints are
**validation-only**: on encode and decode, the runtime evaluates the
constraint expression and returns `ZSERIO_ERR_CONSTRAINT_VIOLATED`
if it is false. No additional bits on the wire.

### 4.3 Default values

No wire impact. Defaults apply at struct **construction** time in
generated code (the per-type `init` helper) and at deserialization
when an extend field is absent (see §6.4).

### 4.4 Functions

Schema functions are pure expressions over struct state. No wire impact
— they are computed at call time in generated C (`static inline`
function returning the expression result).

## 5. Arrays

Element encoding inside an array is identical to standalone field
encoding for the element type. The array variants differ only in how
the **count** is communicated and in whether **packing** is applied.

### 5.1 Fixed-size arrays — `T x[N]`

`N` is a constant. The wire stream contains `N` consecutive elements
with no length prefix.

### 5.2 Length-prefixed arrays — `T x[expr]`

`expr` evaluates against in-scope fields (almost always a sibling
length field). The wire contains exactly `expr` elements with no own
length prefix; the length is communicated by whatever encodes `expr`'s
backing field.

### 5.3 Auto-length arrays — `T x[]`

Wire layout: `varsize count` followed by `count` elements.

### 5.4 Implicit arrays — `T x` (last field, no length)

No length on the wire. Decoder consumes the remaining buffer matching
elements of type `T` until the buffer is exhausted on an exact element
boundary. Per REQ-FS-005, our decode requires the caller to pass an
explicit upper bound on element count; the runtime stops at
min(remaining, caller_bound).

Implicit arrays are restricted by upstream to fixed-size element types;
variable-length elements cannot be used because the decoder has no way
to detect end-of-array.

### 5.5 Packed arrays — `packed T x[…]`

For element types that are integral or compounds containing integral
fields, packing applies delta compression per packable subfield.

The packing protocol uses a `PackingDescriptor` per packable subfield:

```
struct PackingDescriptor {
    bool isPacked;
    bit:6 maxBitNumber if isPacked;
};
```

- `isPacked = 1` selects delta-encoded layout: write the first element's
  raw value, then each subsequent element as a signed delta of width
  `maxBitNumber + 1` bits (the `+1` is for the sign).
- `isPacked = 0` selects unpacked layout: write all elements as their
  raw values. Used when the deltas would be larger than the raw values
  (negative compression).

`maxBitNumber` is computed by the encoder by scanning the array once
before writing — it is the number of value bits required for the largest
absolute delta in the array.

**For arrays of compounds**: each packable subfield gets its own
`PackingDescriptor`, written at the position of that subfield in the
**first** element (Encoding Guide §Packed Arrays of Compounds). The
descriptors are interleaved with the first element's data, not gathered
at the array start. Unpackable subfields (e.g. `string`) are written
unmodified per element.

**For arrays of choices/unions**: deltas are computed per-tag — a
delta runs from the most recent prior element with the same tag.

**For arrays of optionals**: deltas span only set values; absent values
are still written as a single presence bit.

**Nested packable arrays**: behave automatically as packed; the inner
array writes its own `PackingDescriptor`s alongside the outer.

**Upstream**: `DeltaContext` (`compiler/extensions/cpp/runtime/src/zserio/DeltaContext.h`).
The `init` method scans elements and accumulates `m_maxBitNumber`; the
`bitSizeOf` / `read` / `write` methods then operate on the resolved
descriptor. Each call sequence is **stateful** and one-shot — a context
is constructed, initialized over all elements, then used for either
size, read, or write, in lockstep with the element sequence.

**Runtime API sketch**:

```c
typedef struct zserio_delta_ctx {
    /* opaque; sized in header so callers can stack-allocate */
    uint64_t _previous;
    uint32_t _num_elements;
    uint8_t  _max_bit_number;
    uint8_t  _flags;
    /* … */
} zserio_delta_ctx_t;

void  zserio_delta_init        (zserio_delta_ctx_t *ctx);
void  zserio_delta_observe_u32 (zserio_delta_ctx_t *ctx, uint32_t element);
/* observe_* per element type — accumulates max bit number */
zserio_status_t zserio_delta_write_descriptor(zserio_delta_ctx_t *ctx,
                                              zserio_bitstream_t *bs);
zserio_status_t zserio_delta_write_u32 (zserio_delta_ctx_t *ctx,
                                        zserio_bitstream_t *bs,
                                        uint32_t element);
/* … read counterparts … */
```

The context lives on the caller's stack (no heap), is sized at compile
time, and is per-array-instance (REQ-FS-007). One context per packable
subfield in a compound array.

## 6. Layout features

### 6.1 Alignment

`align(N)` before a field forces the bit cursor to the next multiple of
`N` bits. Padding bits are zero. `N` is a power of 2 in the range
[1, 64] in practice (no validation in the encoding guide; we will
enforce at parse time).

If the cursor is already aligned, no padding is emitted.

```
struct Foo {
    bit:11 a;         // bits 0–10
align(32):
    uint32 b;         // padding bits 11–31, then b at 32–63
};
```

### 6.2 Offsets — `offset_field:` binding

```
struct Tile {
    uint32 stringOffset;
    bit:11 a;
stringOffset:
    StringTable stringTable;
};
```

Semantics:
1. The field referenced by the `offset_field:` is implicitly aligned to
   8 bits (i.e. its decode position is rounded up to the next byte).
2. The offset field stores the **byte offset of the dependent field
   from the start of the enclosing struct**.
3. On encode, the encoder backfills the offset value after the dependent
   field's position is known. This requires a two-pass write or an
   in-place patch — generators typically choose the latter.
4. On decode, the runtime reads the offset, validates it against the
   actual position reached, and returns `ZSERIO_ERR_OFFSET_MISMATCH`
   on disagreement.

### 6.3 Indexed offsets

Same as offsets but with `offsets[@index]:` — each element of the
array gets its own offset slot, and each element is byte-aligned at
encode/decode time. Useful for random-access into a sequence of
variable-size elements without scanning prefixes.

### 6.4 Extended members (`extend`)

Recap from `01-schema-language.md` §6.3:
- Each extend field is **byte-aligned** before its bits.
- On decode, the probe `alignTo(8, current_pos) >= buffer_bit_size`
  determines absent vs. present. If absent, take default and stop
  reading further extends.
- On encode, all extend fields are written unconditionally.

The encoder always produces "latest version" output; the decoder
gracefully accepts older versions whose payload ends before all
extends were emitted.

## 7. Schema features with no wire impact

For completeness, these schema features add zero bits to the wire:

- `const` — compile-time constants, never encoded.
- `subtype` — type alias; encoding follows the underlying type.
- Parameterized types — parameters are passed in by the caller (or
  by the enclosing field) and are NOT on the wire.
- Templates — instantiations are distinct types; each instantiation
  encodes per its underlying schema.
- Functions — pure expressions over state, never encoded.
- Constraints — validation-only (§4.2).
- Default values — applied at construction or absence (§4.3, §6.4).
- Annotations — schema metadata; never encoded.
- Comments / doc comments — never encoded.
- Packages and imports — namespace organization; never encoded.

## 8. Implications for the C runtime — concrete header set

This document, together with `01-schema-language.md`, fixes the
**public** runtime API surface for Phase 1. Header layout:

```
runtime/include/zserio/
├── allocator.h     — zserio_allocator_t vtable and default backends
├── status.h        — zserio_status_t and ZSERIO_TRY macro
├── bitstream.h     — zserio_bitstream_t, position / align / size
├── primitives.h    — read/write for every fixed-width integer + bool
├── bits.h          — read/write for bit:N and int:N (variable width)
├── varint.h        — read/write for varint*, varuint*, varsize
├── float.h         — read/write float16/32/64 + the conversion helpers
├── string.h        — string view, owned, read/write
├── extern.h        — bit-view, owned, read/write
├── bytes.h         — bytes view, owned, read/write
├── delta.h         — DeltaContext for packed arrays
├── offset.h        — offset/indexed-offset helpers
└── zserio.h        — umbrella include
```

Internal headers (under `runtime/src/`) hold implementation details
and are not part of the public API.

Generated code only includes the public headers. The runtime itself
is < ~3000 lines of C99 implementation if the surface is held to this.

## 9. Open questions for Phase 1

1. **Offset on-encode strategy**: in-place patch (one-pass write that
   remembers the offset slot's bit position and writes the value into
   it after the dependent field is sized) vs. two-pass write (size
   first, then write). In-place patch is more efficient; two-pass is
   simpler. Recommend in-place patch — it is what upstream's writer
   uses, via the `getBitPosition` / `setBitPosition` pair on
   `BitStreamWriter` (the writer can seek backward to a remembered
   bit position, overwrite the offset slot, then seek forward again).
2. **Bitstream reader/writer state**: does the reader own the source
   buffer (e.g., for materialize) or only borrow? Recommend always
   borrow on construction; the borrower (the materializing read API)
   independently owns its output via the allocator. Aligns with
   REQ-FS-001 and REQ-FS-007.
3. **Endianness of the host vs. wire**: the wire is BE. We do not
   require a BE host; we always do byte-by-byte assembly. CI must
   exercise both LE and BE hosts (or equivalent simulation) at some
   point — but day-one CI on LE x86 is fine.
4. **`alignTo` overflow**: if `current_pos + (N - 1)` would overflow
   `size_t`, what happens? Practically impossible (buffers are bounded
   by `size_t` already), but spec it: runtime returns
   `ZSERIO_ERR_INVALID_ARGUMENT`. Cheap to enforce; expensive to
   regret.
5. **Float16 on targets without native support**: keep the integer-only
   conversion (matches upstream `convertUInt16ToFloat`); never emit
   `_Float16` in generated code.

These are not blocking 02 — they are Phase 1 design notes. None of them
change the wire format itself; they are runtime behavior choices.

## 10. Acceptance

This document is *accepted* when:

- [x] Every v1 construct from `01-schema-language.md` has a wire-layout
      specification here, with at least one upstream citation per
      non-trivial construct.
- [x] The runtime header set in §8 is a complete cover of the API
      surface implied by the wire format.
- [ ] Open questions in §9 are tracked for Phase 1 (not necessarily
      resolved now).

The first two are checked. The third remains open for follow-up work
during Phase 1, which is the appropriate place for runtime API
implementation choices.
