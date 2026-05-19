# Wire format reference

**Version:** 1.0 (compatible with upstream Zserio BIN 1.0; byte-equal with upstream zserio v2.18.1).

**Status:** normative for zserio-c. Discharges [REQ-DM-005](../../requirements/domain/REQ-DM-005.md).

**Origin:** This document is adapted from upstream zserio's `ZserioEncodingGuide.md` (BSD-3-Clause, © Navigation Data Standard e.v.) at pinned version v2.18.1. The adaptation preserves all normative content; structural and editorial changes are zserio-c project conventions.

**Audience:** independent implementers of a zserio codec; integrators who need precise byte-level semantics for safety analysis.

---

## 1. Bitstream conventions

A zserio buffer is a contiguous sequence of bytes whose bit positions are
indexed starting at zero. Every primitive in this document SHALL be encoded
into and decoded from the bitstream under the conventions below; any
deviation by an implementation is a defect.

### 1.1 Bit order within a byte

Within each byte, the **most significant bit comes first**. The first bit
written to a fresh stream lands in bit 7 of byte 0 (the `0x80` position).
Field widths shorter than one byte SHALL consume that many bits starting
at the next free MSB-side position; the bitstream does not insert padding
when a field crosses a byte boundary unless the schema declares
[alignment](#9-alignment-and-offsets) or an [offset](#92-offsets).

### 1.2 Byte order for multi-byte primitives

Multi-byte integers, bit-fields wider than eight bits, and the integer
bit-patterns of IEEE 754 floats SHALL be written **big-endian**: the most
significant byte is emitted first.

### 1.3 Sign representation

Signed integers (`intN`, `int:N`, fixed `int<N>`, varint families) SHALL
use two's-complement representation, sign-extended into the on-wire bit
width. Hex byte `FF` therefore decodes as `255` when read as `uint8` and
as `-1` when read as `int8`.

### 1.4 Position semantics

The bitstream position SHALL advance by exactly the bit width of each
field written or read; no implicit padding is added between fields except
where the schema asks for it. A read past the end of the buffer SHALL
fail with `ZSERIO_ERR_END_OF_STREAM`. The position is a logical bit
counter independent of how the implementation buffers bytes.

### 1.5 Bitstream size cap

The total bit-length of a single zserio bitstream SHALL NOT exceed
`2^31 - 1` bits. The cap exists because every length-bearing wire field
in zserio is encoded as `varsize`, which itself is capped at `2^31 - 1`
(see [§2.4](#24-variable-integer-types)).

### 1.6 Cartoon view

The following example anchors the conventions. Schema:

```zserio
package demo;

struct Tiny
{
    bit:3 a;     // value 5
    bit:5 b;     // value 17
    uint8 c;     // value 0xC3
};
```

Encoded buffer:

```
Byte 0     Byte 1
1011 0001  1100 0011
\_/\_____/ \_______/
 a    b        c
```

The three bits of `a` occupy bits 7..5 of byte 0; the five bits of `b`
occupy bits 4..0 of byte 0; the eight bits of `c` occupy all of byte 1.
No padding separates `a` from `b` or `b` from `c`.

---

## 2. Built-in types

### 2.1 Integer types (`intN`, `uintN`)

The fixed-width integer family covers `int8`, `int16`, `int32`, `int64`
and the matching `uintN`. Each value SHALL be encoded as a `N`-bit
big-endian two's-complement (signed) or unsigned integer at the current
bitstream position. No padding precedes or follows the value.

**Example.** Decimal `513` as `int16`:

```
Offset   00 01
00000000 02 01
```

| Byte position | Value | Hex   | Bits                 | Notes |
| ------------- | ----- | ----- | -------------------- | ----- |
| 0..1          | 513   | 02 01 | 0000 0010 0000 0001  | MSB of bit 0 of byte 0; LSB at bit 0 of byte 1. |

**Example.** Decimal `-513` as `int16`:

```
Offset   00 01
00000000 FD FF
```

| Byte position | Value | Hex   | Bits                 | Notes |
| ------------- | ----- | ----- | -------------------- | ----- |
| 0..1          | -513  | FD FF | 1111 1101 1111 1111  | Bit 15 (the sign bit) is the first bit on the wire. |

### 2.2 Bit-field types (`bit:N`, `int:N`, `bit<expr>`, `int<expr>`)

Bit-fields SHALL be encoded as `N` bits, big-endian (most significant
bit first), starting at the current position. When `N` is not a multiple
of eight, exactly `N` bits are consumed — there is no padding to a byte
boundary. Signed `int:N` uses two's-complement sign extension within the
declared width.

Dynamic-width variants (`bit<expr>`, `int<expr>`) evaluate `expr` against
the surrounding parameters and field values at the moment the field is
encountered, and use the result as the bit width. Width MUST be in
`[1, 64]`.

**Example.** Decimal `513` as `bit:12`:

```
Offset   00 01
00000000 20 10
```

| Byte position | Value | Hex   | Bits          | Notes |
| ------------- | ----- | ----- | ------------- | ----- |
| 0..1          | 513   | 20 10 | 0010 0000 0001 | Only the top 4 bits of byte 1 are part of the field. |

### 2.3 Floating-point types (`float16`, `float32`, `float64`)

Each value SHALL be encoded as its IEEE 754 bit pattern at the
corresponding width: 16 bits (half-precision), 32 bits (single), or 64
bits (double). The bit pattern itself is then written as a big-endian
integer of that width per §1.2. NaN and infinity bit patterns are
preserved verbatim.

The `float16` format is IEEE 754 half-precision: 1 sign bit, 5 exponent
bits, 10 mantissa bits. The host runtime's wider representation is
narrowed using the upstream-defined algorithm (round-to-nearest, ties
to even, with the usual subnormal / overflow handling).

**Example.** `8.0f` as `float16`:

```
Offset   00 01
00000000 48 00
```

| Byte position | Value | Hex   | Bits                 | Notes |
| ------------- | ----- | ----- | -------------------- | ----- |
| 0..1          | 8.0   | 48 00 | 0100 1000 0000 0000  | Sign 0, exponent 10010 (=18 → unbiased +3), mantissa 0; `1.0 × 2^3 = 8.0`. |

### 2.4 Variable integer types

Variable-length integers carry one continuation bit per encoded byte and,
for the signed flavours, one sign bit in the first byte. The number of
payload bytes consumed depends on the value; the maximum is fixed per
type. Within each byte, the **most significant bit is the continuation
flag** ("has next byte"): `1` means another payload byte follows; `0`
means the last payload byte. Payload bits are MSB-first within each
byte and are concatenated MSB-first across bytes to reconstruct the
unsigned magnitude.

For signed flavours (`varintN`, `varint`), the most significant bit of
the **first** byte holds the sign (`1` = negative); the next bit is the
continuation flag; the remaining six bits hold the most significant
magnitude bits. Negation is by sign-magnitude in the encoding, not
two's-complement: a value `-x` encodes the magnitude of `x` and sets the
sign bit. The decoded result is then materialised as a two's-complement
integer of the target width.

The byte layouts:

| Type        | Byte index | Layout |
| ----------- | ---------- | ------ |
| `varint16`  | 0          | sign · has-next · 6 magnitude bits |
|             | 1          | 8 magnitude bits |
| `varuint16` | 0          | has-next · 7 magnitude bits |
|             | 1          | 8 magnitude bits |
| `varint32`  | 0          | sign · has-next · 6 magnitude bits |
|             | 1..2       | has-next · 7 magnitude bits |
|             | 3          | 8 magnitude bits |
| `varuint32` | 0..2       | has-next · 7 magnitude bits |
|             | 3          | 8 magnitude bits |
| `varint64`  | 0          | sign · has-next · 6 magnitude bits |
|             | 1..6       | has-next · 7 magnitude bits |
|             | 7          | 8 magnitude bits |
| `varuint64` | 0..6       | has-next · 7 magnitude bits |
|             | 7          | 8 magnitude bits |
| `varint`    | 0          | sign · has-next · 6 magnitude bits |
|             | 1..7       | has-next · 7 magnitude bits |
|             | 8          | 8 magnitude bits |
| `varuint`   | 0..7       | has-next · 7 magnitude bits |
|             | 8          | 8 magnitude bits |
| `varsize`   | 0          | has-next · 2..7 magnitude bits (see note) |
|             | 1..3       | has-next · 7 magnitude bits |
|             | 4          | 8 magnitude bits |

The minimum encoded size is one byte regardless of type. Subsequent bytes
are present only when the previous byte's continuation bit is `1`. The
encoder SHALL choose the shortest representation that fits the value;
the decoder SHALL accept any well-formed encoding within the type's bit
budget.

> **`varsize` cap.** The maximum value encodable as `varsize` is
> `2^31 - 1`. Because the encoding admits up to five bytes, only `31`
> magnitude bits across those five bytes carry information; the high
> bits of the first byte beyond bit position 30 of the magnitude SHALL
> be zero on write. For example, the first byte of the maximum value
> `2^31 - 1` is `0x83` (continuation set, low two bits set). A decoder
> reading a `varsize` whose magnitude exceeds `2^31 - 1` SHALL fail
> with `ZSERIO_ERR_INVALID_ENCODING`.

**Encoded ranges.** Because every byte costs at least one continuation
bit and the first byte of a signed variant costs an extra sign bit, the
representable range of each variant is narrower than the underlying C
type would otherwise suggest:

| Type        | Bytes | Magnitude bits | Encoded range |
| ----------- | ----- | -------------- | ------------- |
| `varuint16` | 1..2  | 7 + 8 = 15     | `[0, 2^15 - 1]` |
| `varuint32` | 1..4  | 7·3 + 8 = 29   | `[0, 2^29 - 1]` |
| `varuint64` | 1..8  | 7·7 + 8 = 57   | `[0, 2^57 - 1]` |
| `varuint`   | 1..9  | 7·8 + 8 = 64   | `[0, 2^64 - 1]` |
| `varsize`   | 1..5  | capped at 31   | `[0, 2^31 - 1]` |
| `varint16`  | 1..2  | 6 + 8 = 14     | `[-(2^14 - 1), 2^14 - 1]` |
| `varint32`  | 1..4  | 6 + 7·2 + 8    | `[-(2^28 - 1), 2^28 - 1]` |
| `varint64`  | 1..8  | 6 + 7·6 + 8    | `[-(2^56 - 1), 2^56 - 1]` |
| `varint`    | 1..9  | 6 + 7·7 + 8    | `[-(2^63 - 1), 2^63 - 1]` plus a 1-byte special form for `INT64_MIN` |

The `varint` variant SHALL additionally encode `INT64_MIN` using a
1-byte special form `sign=1, has-next=0, magnitude=0`. No other signed
variant supports a value outside its symmetric range; encoders SHALL
reject out-of-range inputs.

### 2.5 Boolean type (`bool`)

A `bool` SHALL be encoded as exactly one bit. `true` is `1`, `false` is
`0`. The bit occupies the next position in the stream and does not pad
to a byte boundary.

### 2.6 String type (`string`)

A `string` SHALL be encoded as a `varsize` byte-length prefix followed
by that many bytes of UTF-8 payload. The prefix counts encoded UTF-8
bytes, not Unicode code points and not characters. The payload is
written byte-aligned at the prefix's current position only when the
preceding bitstream position is byte-aligned; otherwise the payload
bytes are written MSB-first at the current sub-byte position, exactly
as if they were eight independent `bit:8` fields.

| Field       | Width        |
| ----------- | ------------ |
| length      | `varsize`    |
| payload     | `length × 8` bits |

**Example.** The string `"Zserio is cool"`:

```
Offset   00 01 02 03 04 05 06 07  08 09 10 11 12 13 14
00000000 0E 5A 73 65 72 69 6F 20  69 73 20 63 6F 6F 6C
```

| Byte position | Value             | Hex                                       | Notes |
| ------------- | ----------------- | ----------------------------------------- | ----- |
| 0             | 14                | 0E                                        | length as `varsize`. |
| 1..14         | `"Zserio is cool"` | 5A 73 65 72 69 6F 20 69 73 20 63 6F 6F 6C | UTF-8 bytes. |

### 2.7 Extern type (`extern`)

An `extern` field SHALL be encoded as a `varsize` **bit-length** prefix
followed by that many raw bits, written at the current bitstream
position. The total bit-length of an `extern` field SHALL NOT exceed the
overall `2^31 - 1` bitstream cap.

| Field       | Width        |
| ----------- | ------------ |
| bit-length  | `varsize`    |
| payload     | `bit-length` bits |

**Example.** The 10-bit sequence `1010010111`:

```
Offset   00 01 02
00000000 0A A5 C0
```

| Byte position | Value          | Hex   | Notes |
| ------------- | -------------- | ----- | ----- |
| 0             | 10             | 0A    | bit-length as `varsize`. |
| 1..2          | `1010010111`   | A5 C0 | payload; bits 0..9 of bytes 1..2. The trailing six bits of byte 2 are not part of the field — they belong to whatever follows. |

### 2.8 Bytes type (`bytes`)

A `bytes` field SHALL be encoded as a `varsize` byte-length prefix
followed by that many bytes of raw payload. The payload semantics match
§2.6: byte-aligned when the prefix ends on a byte boundary, otherwise
written as a sequence of eight-bit fields at the current sub-byte
position.

| Field       | Width        |
| ----------- | ------------ |
| byte-length | `varsize`    |
| payload     | `byte-length × 8` bits |

**Example.** Payload `de ad be ef`:

```
Offset   00 01 02 03 04
00000000 04 DE AD BE EF
```

| Byte position | Hex         | Notes |
| ------------- | ----------- | ----- |
| 0             | 04          | byte-length as `varsize`. |
| 1..4          | DE AD BE EF | payload. |

---

## 3. Enumeration types

An `enum` SHALL be encoded as its declared underlying type, using the
encoding rules of §2 for that underlying type. The enumerated label is
not transmitted; only the numeric value is. An encoder SHALL refuse to
write a value not declared by the enum; a decoder SHALL accept any
declared value and surface
`ZSERIO_ERR_INVALID_VALUE` (or equivalent) for an undeclared one.

**Example.**

```zserio
enum bit:3 Color
{
    NONE  = 000b,
    RED   = 010b,
    BLUE,
    BLACK = 111b
};
```

The value `RED` is encoded as the three-bit pattern `010`.

See [`schema-language.md`](schema-language.md) for the precise legal-
underlying-type set and [`types-mapping.md`](types-mapping.md) for the
C-side representation.

---

## 4. Bitmask types

A `bitmask` SHALL be encoded as its declared underlying type, identical
to an enum (§3). The on-wire value is the bitwise-OR of the declared
flag values present. Bits not corresponding to any declared flag are
implementation-defined on read but SHALL be preserved on a round-trip.

**Example.**

```zserio
bitmask uint8 Permission
{
    EXECUTABLE,
    READABLE = 0x02,
    WRITABLE
};
```

The single-flag value `READABLE` is encoded as `00000010`:

```
Offset   00
00000000 02
```

---

## 5. Structure types

A `struct` SHALL be encoded by emitting each field in source declaration
order, using the encoding rules for that field's type. No tag, length
prefix, or padding is inserted between fields. Field boundaries cross
byte boundaries freely; padding appears only where the schema declares
[alignment](#9-alignment-and-offsets) or an [offset](#92-offsets).

**Example.**

```zserio
struct MyStructure
{
    bit:4 a = 7;
    uint8 b = 127;
    bit:4 c = 13;
};
```

Encoded with default values:

```
Offset   00 01
00000000 77 FD
```

| Byte | Bit positions | Value | Hex | Field |
| ---- | ------------- | ----- | --- | ----- |
| 0    | 0..3          | 7     | 7   | `a`, 4 bits |
| 0..1 | 4..11         | 127   | 7F  | `b`, 8 bits crossing the byte boundary |
| 1    | 12..15        | 13    | D   | `c`, 4 bits |

> Note that field `b` crosses the byte-0/byte-1 boundary even though
> `MyStructure` is itself byte-aligned overall. Embedded into a larger
> compound, `MyStructure` may begin at any bit position; no realignment
> is applied at type boundaries.

The wire-level consequences of `struct` field ordering — that any
schema edit which reorders, retypes, or inserts a field changes the
wire format — are covered in
[`schema-evolution.md`](schema-evolution.md).

---

## 6. Choice types

A `choice` SHALL be encoded as exactly the data of the selected case
arm, written per the rules for that case's type. The selector parameter
is **not** part of the wire payload — it must be available to the
decoder as a parameter and SHALL come from the surrounding context (a
parameter, a previously-encoded field, or a parent's parameter list).

If no case matches the selector and a `default` arm is declared, the
default arm's data is encoded; if no case matches and there is no
`default`, the choice contributes **zero bits** to the stream.

**Example.**

```zserio
choice VarCoordXY(uint8 width) on width
{
    case  8: bit:8  coord8;
    case 16: bit:16 coord16;
    case 24: bit:24 coord24;
    case 32: bit:32 coord32;
};
```

For `width = 24` and `coord24 = 0xBEDEAD`, the encoding is exactly the
24-bit `coord24` field:

```
Offset   00 01 02
00000000 BE DE AD
```

---

## 7. Union types

A `union` SHALL be encoded as a `varsize` choice tag followed by the
selected branch's data. The choice tag is the zero-based source-order
index of the selected case (the first declared branch has tag `0`).

| Field      | Width        |
| ---------- | ------------ |
| choice tag | `varsize`    |
| branch     | width of selected branch's type |

**Example.**

```zserio
union SimpleUnion
{
    uint8   value8;
    uint16  value16;
};
```

With the `value16` branch selected and value `0xDEAD`:

```
Offset   00 01 02
00000000 01 DE AD
```

| Byte position | Value | Hex   | Notes |
| ------------- | ----- | ----- | ----- |
| 0             | 1     | 01    | choice tag as `varsize`; branch index `1`. |
| 1..2          | 0xDEAD | DE AD | `uint16` payload. |

A `union` is wire-equivalent to a `struct` carrying a `varsize` tag
followed by a parameter-driven `choice`. The two schemas below produce
byte-equal output for any value, with the tag of the `choice` rewrite
shifted into the `ChoiceTag` enum:

```zserio
struct SimpleUnion
{
    ChoiceTag   choiceTag;
    SimpleValue simpleValue(choiceTag);
};

enum varsize ChoiceTag
{
    TAG_VALUE8  = 0,
    TAG_VALUE16 = 1
};

choice SimpleValue(ChoiceTag choiceTag) on choiceTag
{
    case TAG_VALUE8:
        uint8   value8;
    case TAG_VALUE16:
        uint16  value16;
};
```

---

## 8. Optional members

Optional fields come in two surface syntaxes: auto-optional
(`optional T`) and conditional (`T field if expr`). They share a single
wire form when the `expr` form's gating expression is observable to the
decoder; the auto form simply transmits the presence bit explicitly.

### 8.1 Auto-optional (`optional`)

An auto-optional field SHALL be encoded as one bit of **presence flag**
followed, when the flag is `1`, by the field's value. When the flag is
`0` the field contributes exactly one bit to the stream.

| Bit position | Width  | Field |
| ------------ | ------ | ----- |
| 0            | 1 bit  | presence flag |
| 1..          | type-dependent | value (only present when flag = 1) |

**Example.**

```zserio
struct Container
{
    optional int32 autoOptionalInt;
};
```

With `autoOptionalInt = 1054780911` (`0x3EDEADEF`):

```
Offset   00 01 02 03 04
00000000 9F 6F 56 F7 80
```

| Byte | Bit positions | Value      | Hex          | Notes |
| ---- | ------------- | ---------- | ------------ | ----- |
| 0    | 0             | 1          | 1            | presence flag — value follows. |
| 0..4 | 1..32         | 1054780911 | 3E DE AD EF  | `int32`, 32 bits beginning at bit 1; the low seven bits of byte 4 are not part of the field. |

The total field width is 33 bits.

With the value absent, the field contributes the single bit `0`.

### 8.2 Conditional (`if`)

A conditional field SHALL be encoded **only when its guarding
expression evaluates to true**; no presence bit is added. The decoder
reproduces the same evaluation against the values it has already
decoded, so the gate is observable to both sides.

```zserio
struct Container
{
    bool  hasOptionalInt;
    int32 optionalInt if hasOptionalInt;
}
```

This schema is wire-equivalent to the `optional int32` example above:
the `bool` plays the role of the presence flag.

### 8.3 `extend`

An `extend` field is a tail-position evolution mechanism. On the encode
path it behaves as an unconditional field. On the decode path the field
is treated as present iff at least one bit remains in the bitstream when
the field's encoding begins; otherwise the decoder synthesises a default
value and reads no further. See
[`schema-evolution.md`](schema-evolution.md) for the legal placement
rules and the round-trip guarantees that follow from them.

---

## 9. Array types

Every array variant SHALL be encoded as a sequence of element encodings
in index order. The variants differ only in how the element count
reaches the decoder, and in whether the element encodings are
delta-compressed (`packed`).

The **per-array element count** SHALL NOT exceed `2^31 - 1`; encoders
that observe an over-cap array MUST fail with
`ZSERIO_ERR_VALUE_OUT_OF_RANGE` and decoders SHALL fail with
`ZSERIO_ERR_INVALID_ENCODING` on a wire prefix that would exceed the cap.

### 9.1 Fixed-length and length-expression arrays

When the schema declares an array with a constant length (`T arr[N]`)
or with a length expression (`T arr[expr]`), the on-wire form is `N`
element encodings concatenated back-to-back. No length prefix is added
to the wire; the decoder reads `N` elements where `N` is derived from
the schema constant or evaluated against the surrounding parameters and
fields at decode time.

**Example.**

```zserio
struct ArrayExample
{
    uint8 header[2];
    int16 numItems;
    uint8 list[numItems];
};
```

With `header = [be, eb]`, `numItems = 2`, `list = [ab, ba]`:

```
Offset   00 01 02 03 04 05
00000000 BE EB 00 02 AB BA
```

| Byte | Hex   | Field |
| ---- | ----- | ----- |
| 0    | BE    | `header[0]` |
| 1    | EB    | `header[1]` |
| 2..3 | 00 02 | `numItems`, `int16` |
| 4    | AB    | `list[0]` |
| 5    | BA    | `list[1]` |

> Note that fixed-length and length-expression arrays carry no on-wire
> length payload beyond the element bytes themselves.

### 9.2 Implicit-length arrays (`T arr[]` — implicit case)

An implicit-length array is read by continuing to consume elements
until the bitstream is exhausted. The encoder SHALL emit elements in
order with no count prefix. The decoder SHALL keep reading until the
remaining bit-count is insufficient for another element, at which
point the array ends. Implicit-length arrays can therefore appear only
in tail position of the outermost type.

### 9.3 Auto-length arrays (`T arr[]` — auto case)

An auto-length array SHALL be encoded as a `varsize` element-count
prefix followed by the element encodings.

| Field          | Width        |
| -------------- | ------------ |
| element count  | `varsize`    |
| elements       | per-element-type encoded back-to-back |

**Example.**

```zserio
struct AutoArray
{
    uint8 list[];
};
```

With `list = [be, eb]`:

```
Offset   00 01 02
00000000 02 BE EB
```

| Byte | Hex | Field |
| ---- | --- | ----- |
| 0    | 02  | length as `varsize`. |
| 1    | BE  | `list[0]`. |
| 2    | EB  | `list[1]`. |

An auto-length array is wire-equivalent to:

```zserio
struct AutoArray
{
    varsize numElements;
    uint8 list[numElements];
};
```

### 9.4 Packed arrays — common structure

The `packed` keyword on an array element type SHALL replace the
unpacked element-by-element encoding (§9.1–§9.3) with a delta-compressed
form for the fields that the encoding considers **packable**. Only
fixed-width integer fields (`intN`, `uintN`, `bit:N`, `int:N`,
`bit<expr>`, `int<expr>`), enum and bitmask fields whose underlying
type is packable, and the variable-length integer families
(`varintN`, `varuintN`, `varint`, `varuint`, `varsize`) are packable.
All other fields (strings, externs, bytes, floats, bools, choices and
unions at the field level, sub-compounds) are unpackable and SHALL be
encoded directly.

For each packable scalar position in the element, the encoder builds a
**`PackingDescriptor`**:

```zserio
struct PackingDescriptor
{
    bool  isPacked;
    bit:6 maxBitNumber if isPacked;
};
```

The descriptor's two-field encoding is determined by walking the entire
array up front:

1. Compute the deltas `Δ_i = e_i - e_{i-1}` for `i = 1..N-1`.
2. Let `M` be the largest absolute value among the deltas. `maxBitNumber`
   is `⌈log2(M+1)⌉` — the number of bits needed to represent `M` as an
   **unsigned** magnitude. The delta itself is written with one extra
   bit to carry the sign, i.e. `maxBitNumber + 1` bits, two's-complement.
3. Compute the on-wire cost of the packed form
   (`1 + 6 + unpacked_size(e_0) + (N-1) × (maxBitNumber + 1)`) and of
   the unpacked form (`1 + N × unpacked_size(e_i)`, summing the
   per-element unpacked cost for variable-length types). If the packed
   form does not strictly save bits, set `isPacked = 0` and emit the
   array unpacked; otherwise set `isPacked = 1`.

Special cases:

- All deltas equal zero (a constant array): `maxBitNumber = 0` and no
  delta bits at all are emitted per element after the first. The
  reconstruction trivially repeats `e_0`.
- A single-element array: `maxBitNumber` is `0`; `isPacked` is `1`; the
  first element is then written and no deltas follow.
- An empty array: the descriptor is **not** written. The auto-length
  prefix carries `0` and the array terminates there.

The descriptor itself is `1` bit when `isPacked = 0` and `1 + 6 = 7`
bits when `isPacked = 1`.

### 9.5 Packed arrays of integers

For a packed array of a scalar integer type, the on-wire form is:

| Field      | Width                                                  |
| ---------- | ------------------------------------------------------ |
| descriptor | `PackingDescriptor` (1 or 7 bits)                      |
| `e_0`      | unpacked encoding of the first element                 |
| `e_i` for `i ≥ 1` | when packed: `maxBitNumber + 1` bits signed delta from `e_{i-1}`; when unpacked: unpacked encoding |

The leading element is always written in full because there is no
predecessor. For an auto-length packed array, the `varsize` length
prefix comes first, then the descriptor, then the elements.

**Example — packing wins.**

```zserio
struct PackedArray
{
    packed uint8 list[5];
};
```

With `list = [11, 12, 15, 22, 23]`. Deltas are `[1, 3, 7, 1]`; max
delta is `7`; `maxBitNumber = 3`; delta width is `4` bits including
sign.

```
Offset   00 01 02 03
00000000 86 16 26 E2
```

| Bit positions | Value | Bits        | Field |
| ------------- | ----- | ----------- | ----- |
| 0             | 1     | 1           | `PackingDescriptor.isPacked = 1`. |
| 1..6          | 3     | 000011      | `PackingDescriptor.maxBitNumber = 3`. |
| 7..14         | 11    | 00001011    | `list[0]`, full `uint8`. |
| 15..18        | 1     | 0001        | 4-bit signed delta to `list[1]`. |
| 19..22        | 3     | 0011        | 4-bit signed delta to `list[2]`. |
| 23..26        | 7     | 0111        | 4-bit signed delta to `list[3]`. |
| 27..30        | 1     | 0001        | 4-bit signed delta to `list[4]`. |

The packed form is 31 bits vs. 40 bits unpacked.

**Example — packing loses.**

With `list = [0, 250, 251, 252, 253]`. Deltas are `[250, 1, 1, 1]`,
which would need `maxBitNumber = 8` and a 9-bit delta — yielding
`1 + 6 + 8 + 4 × 9 = 51` bits vs. `1 + 5 × 8 = 41` bits unpacked.
Encoder therefore picks unpacked:

```
Offset   00 01 02 03 04 05
00000000 00 7D 7D FE 7E 80
```

| Bit positions | Value | Bits     | Field |
| ------------- | ----- | -------- | ----- |
| 0             | 0     | 0        | `PackingDescriptor.isPacked = 0`. |
| 1..8          | 0     | 00000000 | `list[0]`. |
| 9..16         | 250   | 11111010 | `list[1]`. |
| 17..24        | 251   | 11111011 | `list[2]`. |
| 25..32        | 252   | 11111100 | `list[3]`. |
| 33..40        | 253   | 11111101 | `list[4]`. |

Total: 41 bits.

The same value is wire-equivalent to the following elaborated schema
without the `packed` modifier — the packed array is a syntactic
convenience, not a separate wire family:

```zserio
struct PackingDescriptor
{
    bool  isPacked;
    bit:6 maxBitNumber if isPacked;
};

struct DeltaPackedArray(bit:6 maxBitNumber)
{
    uint8                  element0;
    int<maxBitNumber + 1>  deltas[4];
};

struct PackedArray
{
    PackingDescriptor                                packingDescriptor;
    DeltaPackedArray(packingDescriptor.maxBitNumber) packedList   if  packingDescriptor.isPacked;
    uint8                                            unpackedList[5] if !packingDescriptor.isPacked;
};
```

### 9.6 Packed arrays of optional members

When an array element is a compound whose field is itself optional, the
delta context for that field SHALL include only the **present**
elements, in their array index order. Absent elements contribute their
presence bit (or evaluate their `if` gate to `false`) and nothing else;
the next present element's delta is taken against the previous present
element.

**Example.**

```zserio
struct Element
{
    optional int8 number;
};

struct PackedArray
{
    packed Element list[5];
};
```

With `list = [none, 11, none, 15, 20]`. The present-only sequence is
`[11, 15, 20]`; deltas are `[4, 5]`; max delta is `5`; `maxBitNumber = 3`,
delta width 4 bits.

```
Offset   00 01 02 03
00000000 61 85 A9 50
```

| Bit position | Value | Bits     | Field |
| ------------ | ----- | -------- | ----- |
| 0            | 0     | 0        | `list[0].number` presence flag — absent. |
| 1            | 1     | 1        | `list[1].number` presence flag — present. |
| 2            | 1     | 1        | `PackingDescriptor.isPacked = 1`. |
| 3..8         | 3     | 000011   | `PackingDescriptor.maxBitNumber = 3`. |
| 9..16        | 11    | 00001011 | `list[1].number` first present element, full `int8`. |
| 17           | 0     | 0        | `list[2].number` absent. |
| 18           | 1     | 1        | `list[3].number` present. |
| 19..22       | 4     | 0100     | 4-bit signed delta `15 − 11`. |
| 23           | 1     | 1        | `list[4].number` present. |
| 24..27       | 5     | 0101     | 4-bit signed delta `20 − 15`. |

> The packed form takes 28 bits vs. 29 bits unpacked. Larger
> underlying types widen the gap.

### 9.7 Packed arrays of compounds

For a packed array of `struct` elements, the encoder threads one
**independent `PackingDescriptor` per packable field** through the
array. The descriptor for a field is written immediately before that
field's first-element value, in the field's source order. After the
descriptors and the full first element are emitted, each subsequent
element contributes either a delta (when its field's descriptor says
`isPacked = 1`) or a full unpacked encoding (when `isPacked = 0`) for
each packable field, and a full unpacked encoding for each unpackable
field — every time, since unpackable fields have no compression.

Schematically, for an N-element packed array of a struct with packable
fields `p1..pk` and unpackable fields `u1..um` in some interleaved
declaration order:

```
[descriptor(p1)][e0.p1 unpacked]                            \
[descriptor(p2)][e0.p2 unpacked]                             |  element 0
... interleaved with u_j fields in source order              /
[e0.u1 unpacked] ... [e0.um unpacked]

[e1.p1 delta-or-unpacked][e1.u1 unpacked] ...               \
[e2.p1 delta-or-unpacked][e2.u1 unpacked] ...                |  elements 1..N-1
...                                                          /
```

**Example.**

```zserio
struct PackableStructure
{
    uint32 value;  // packable
    string text;   // unpackable
};

struct PackedArray
{
    packed PackableStructure list[5];
};
```

With

```
list = [
    PackableStructure( 0, "a"),
    PackableStructure(10, "b"),
    PackableStructure(20, "c"),
    PackableStructure(30, "d"),
    PackableStructure(40, "e"),
]
```

— deltas of `value` are `[10, 10, 10, 10]`, max delta `10`,
`maxBitNumber = 4`, delta width 5 bits — the encoding is:

```
Offset   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F
00000000 88 00 00 00 00 02 C2 A0  16 25 00 B1 A8 05 91 40
00000010 2C A0
```

| Bit positions | Value | Bits / Hex          | Field |
| ------------- | ----- | ------------------- | ----- |
| 0             | 1     | 1                   | `PackingDescriptor.isPacked` for `value`. |
| 1..6          | 4     | 000100              | `PackingDescriptor.maxBitNumber` for `value`. |
| 7..38         | 0     | 00 00 00 00         | `list[0].value`, full `uint32`. |
| 39..46        | 1     | 01                  | length of `list[0].text`. |
| 47..54        | "a"   | 61                  | `list[0].text` UTF-8 byte. |
| 55..59        | 10    | 01010               | 5-bit delta to `list[1].value`. |
| 60..67        | 1     | 01                  | length of `list[1].text`. |
| 68..75        | "b"   | 62                  | `list[1].text` UTF-8 byte. |
| 76..80        | 10    | 01010               | 5-bit delta to `list[2].value`. |
| 81..88        | 1     | 01                  | length of `list[2].text`. |
| 89..96        | "c"   | 63                  | `list[2].text` UTF-8 byte. |
| 97..101       | 10    | 01010               | 5-bit delta to `list[3].value`. |
| 102..109      | 1     | 01                  | length of `list[3].text`. |
| 110..117      | "d"   | 64                  | `list[3].text` UTF-8 byte. |
| 118..122      | 10    | 01010               | 5-bit delta to `list[4].value`. |
| 123..130      | 1     | 01                  | length of `list[4].text`. |
| 131..138      | "e"   | 65                  | `list[4].text` UTF-8 byte. |

The packed encoding takes 139 bits vs. 240 bits unpacked
(`5 × (32 + 16)`).

The wire-equivalent elaborated schema with one explicit
`PackingDescriptor` per packable field per element-zero, followed by
parameterised per-element types, is:

```zserio
struct PackingDescriptor
{
    bool  isPacked;
    bit:6 maxBitNumber if isPacked;
};

struct PackableStructureElement0
{
    PackingDescriptor valuePackingDescriptor;
    uint32            value;
    string            text;
};

struct PackableStructureElementX(PackingDescriptor valuePackingDescriptor)
{
    int<valuePackingDescriptor.maxBitNumber + 1> valueDelta if  valuePackingDescriptor.isPacked;
    uint32                                       value      if !valuePackingDescriptor.isPacked;
    string                                       text;
};

struct PackedArray
{
    PackableStructureElement0                                element0;
    PackableStructureElementX(element0.valuePackingDescriptor) elements[4];
};
```

### 9.8 Packed arrays of choices and unions

In a `choice` element, at most one case arm is active per element; the
others contribute nothing. The packed encoding treats each case arm
**as if** it were an optional field: the per-field delta context for
arm `k`'s payload tracks only those elements that selected arm `k`. The
first element that selects arm `k` writes the arm's `PackingDescriptor`
(for the packable payload field of that arm) followed by the unpacked
first value; subsequent elements that also select arm `k` write deltas
against the previous-same-arm value.

A `union` SHALL be encoded the same way, with the additional twist that
the union tag itself is packable (it is a `varsize`). The first element
writes a `PackingDescriptor` for the tag followed by the tag value;
subsequent elements write tag deltas. The tag delta is computed against
the previous element's tag regardless of which arm was selected.

### 9.9 Packed arrays of nested compounds

When a packable struct field is itself a struct containing packable
fields, the inner packing is **automatic**: those inner fields each
acquire their own `PackingDescriptor` threaded through the outer
array, exactly as if the inner type were also written `packed`. The
encoder emits the inner descriptors interleaved with the outer ones in
source declaration order; the decoder consumes them in the same order.

Nested packing applies recursively. Inner arrays declared as `packed`
embed their own `PackingDescriptor` per packable element field, scoped
to the inner array (not threaded through the outer array).

**Example.**

```zserio
struct InnerStructure
{
    uint64 value64;
    uint16 value16;
};

struct PackableStructure
{
    uint32         value32;
    string         text;
    InnerStructure innerStructure;
};

struct PackedArray
{
    packed PackableStructure list[5];
};
```

With

```
list = [
    PackableStructure(10, "a", InnerStructure(1000, 65535)),
    PackableStructure(20, "b", InnerStructure( 950,     0)),
    PackableStructure(30, "c", InnerStructure(1000, 65535)),
    PackableStructure(40, "d", InnerStructure( 950,     0)),
    PackableStructure(50, "e", InnerStructure(1000, 65535)),
]
```

Per-field analyses:

- `value32`: deltas `[10, 10, 10, 10]`, packed, `maxBitNumber = 4`.
- `text`: unpackable.
- `innerStructure.value64`: deltas `[-50, 50, -50, 50]`, packed,
  `maxBitNumber = 6` (max abs 50 → 6 bits unsigned + 1 sign bit = 7
  bits).
- `innerStructure.value16`: deltas `[-65535, 65535, -65535, 65535]`,
  which would need `maxBitNumber = 16` and 17-bit deltas — worse than
  the 16-bit unpacked baseline, so unpacked.

Resulting bitstream:

```
Offset   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F
00000000 88 00 00 00 00 02 C3 18  00 00 00 00 00 00 0F A1
00000010 FF FE A0 16 29 C0 00 0A  01 63 65 FF FE A0 16 49
00000020 C0 00 0A 01 65 65 FF FE
```

| Bit positions | Value | Bits / Hex     | Field |
| ------------- | ----- | -------------- | ----- |
| 0             | 1     | 1              | `PackingDescriptor.isPacked` for `value32`. |
| 1..6          | 4     | 000100         | `PackingDescriptor.maxBitNumber` for `value32`. |
| 7..38         | 10    | 00 00 00 0A    | `list[0].value32`. |
| 39..46        | 1     | 01             | length of `list[0].text`. |
| 47..54        | "a"   | 61             | `list[0].text`. |
| 55            | 1     | 1              | `PackingDescriptor.isPacked` for `value64`. |
| 56..61        | 6     | 000110         | `PackingDescriptor.maxBitNumber` for `value64`. |
| 62..125       | 1000  | … 03 E8        | `list[0].innerStructure.value64`. |
| 126           | 0     | 0              | `PackingDescriptor.isPacked` for `value16` — not packed. |
| 127..142      | 65535 | FF FF          | `list[0].innerStructure.value16`. |
| 143..147      | 10    | 01010          | 5-bit delta to `list[1].value32`. |
| 148..155      | 1     | 01             | length of `list[1].text`. |
| 156..163      | "b"   | 62             | `list[1].text`. |
| 164..170      | -50   | 1001110        | 7-bit signed delta to `list[1].innerStructure.value64`. |
| 171..186      | 0     | 00 00          | `list[1].innerStructure.value16`. |
| 187..191      | 10    | 01010          | 5-bit delta to `list[2].value32`. |
| 192..199      | 1     | 01             | length of `list[2].text`. |
| 200..207      | "c"   | 63             | `list[2].text`. |
| 208..214      | 50    | 0110010        | 7-bit signed delta to `list[2].innerStructure.value64`. |
| 215..230      | 65535 | FF FF          | `list[2].innerStructure.value16`. |
| 231..235      | 10    | 01010          | 5-bit delta to `list[3].value32`. |
| 236..243      | 1     | 01             | length of `list[3].text`. |
| 244..251      | "d"   | 64             | `list[3].text`. |
| 252..258      | -50   | 1001110        | 7-bit signed delta to `list[3].innerStructure.value64`. |
| 259..274      | 0     | 00 00          | `list[3].innerStructure.value16`. |
| 275..279      | 10    | 01010          | 5-bit delta to `list[4].value32`. |
| 280..287      | 1     | 01             | length of `list[4].text`. |
| 288..295      | "e"   | 65             | `list[4].text`. |
| 296..302      | 50    | 0110010        | 7-bit signed delta to `list[4].innerStructure.value64`. |
| 303..318      | 65535 | FF FF          | `list[4].innerStructure.value16`. |

> 319 bits vs. 640 bits unpacked (`5 × (32 + 16 + 64 + 16)`). The
> `value16` field is not packed because the all-bits-significant deltas
> would inflate the encoding.

The wire-equivalent elaborated schema is mechanical and follows the
same pattern as §9.7 with one extra parameter row per inner packable
field — element zero records a `PackingDescriptor` for each packable
field in source order, and element X is parameterised on the
corresponding tuple of descriptors and writes a delta or a full value
per field accordingly.

### 9.10 Packed arrays of nested arrays

An inner array declared `packed` inside a packable struct field is
**always** packed by its own `PackingDescriptor`, computed afresh per
outer element. The inner descriptor lives at the inner array's
position; there is no threading of the inner array's deltas across
outer elements. Equivalently: nested packing scopes follow the schema's
declared nesting, never the outer array iteration.

---

## 10. Alignment and offsets

### 10.1 Alignment (`align(N):`)

The `align(N):` directive inserts zero-padding bits before the next
field so that the bitstream position becomes a multiple of `N` bits.
`N` SHALL be a positive integer; alignment to 8 (byte boundary), 16,
32, and 64 are the common cases.

If the position is already a multiple of `N`, zero bits of padding are
inserted. Otherwise exactly `((N − (pos mod N)) mod N)` zero bits are
inserted.

**Example.**

```zserio
struct AlignmentExample
{
    bit:11 a;

align(32):
    uint32 b;
};
```

| Bit positions | Field |
| ------------- | ----- |
| 0..10         | `a`. |
| 11..31        | 21 zero-padding bits to reach a multiple of 32. |
| 32..63        | `b`. |

> Total: 64 bits (8 bytes). Without the directive the struct would be
> 43 bits.

### 10.2 Offsets

An `offset:` declaration records the bitstream position of the next
field into a previously-encoded numeric field. The recorded value is
the **byte position**, and the offsetted field is implicitly aligned to
the next byte boundary (i.e. an implicit `align(8)`) before its
encoding begins.

The encoder SHALL fill in the offset field with the correct byte
position once that position is known. The decoder SHALL compare the
recorded offset against the actual position observed at the offsetted
field's start; on mismatch the decoder SHALL fail with
`ZSERIO_ERR_OFFSET_MISMATCH`.

**Example.**

```zserio
struct OffsetExample
{
    uint32 offset;
    bit:11 a;

offset:
    uint16 b;
};
```

| Bit positions | Field |
| ------------- | ----- |
| 0..31         | `offset`. |
| 32..42        | `a`. |
| 43..47        | 5 zero-padding bits, byte alignment forced by `offset:`. |
| 48..63        | `b`. |

> Total: 64 bits. Without the directive the struct would be 59 bits.

Offsets can be filled in automatically by the encoder before the actual
write pass; an integrator using the runtime is responsible for calling
the offset-initialiser for the type before serialising it.

### 10.3 Indexed offsets

An `offset[@index]:` declaration is the array-valued generalisation of
§10.2: the offsetted field is itself an array, and each element is
aligned to a byte boundary and recorded into the corresponding
parallel-array offset slot. The array of offsets MUST have the same
length as the offsetted array; the application is responsible for
sizing it before encoding.

**Example.**

```zserio
struct IndexedOffsetsExample
{
    uint32 offsets[2];
    bit:1  spacer;

offsets[@index]:
    bit:5  data[2];
};
```

| Bit positions | Field |
| ------------- | ----- |
| 0..31         | `offsets[0]`. |
| 32..63        | `offsets[1]`. |
| 64            | `spacer`. |
| 65..71        | 7 zero-padding bits to byte-align `data[0]`. |
| 72..76        | `data[0]`. |
| 77..79        | 3 zero-padding bits to byte-align `data[1]`. |
| 80..84        | `data[1]`. |

The decoder SHALL check each `offsets[i]` against the actual byte
position of `data[i]` and fail with `ZSERIO_ERR_OFFSET_MISMATCH` on
divergence.

---

## 11. Worked end-to-end example

The following example anchors the rules above against a complete
bytestream, in the spirit of upstream's *Simple Example*.

```zserio
package tutorial;

struct Employee
{
    uint8  age;
    string name;
    uint16 salary;
    Role   role;
};

enum uint8 Role
{
    DEVELOPER = 0,
    TEAM_LEAD = 1,
    CTO       = 2,
};
```

With `age = 32`, `name = "Joe Smith"`, `salary = 5000`,
`role = DEVELOPER`:

```
Offset   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D
00000000 20 09 4A 6F 65 20 53 6D  69 74 68 13 88 00
```

| Byte position | Value             | Hex                        | Source |
| ------------- | ----------------- | -------------------------- | ------ |
| 0             | 32                | 20                         | `age`, `uint8` per §2.1. |
| 1             | 9                 | 09                         | `name` length, `varsize` per §2.4. |
| 2..10         | `"Joe Smith"`     | 4A 6F 65 20 53 6D 69 74 68 | `name` UTF-8 bytes per §2.6. |
| 11..12        | 5000              | 13 88                      | `salary`, `uint16` per §2.1. |
| 13            | 0                 | 00                         | `role`, `uint8` per §3. |

Total: 14 bytes. The struct begins on a byte boundary and every field
in it is byte-multiple-width, so no sub-byte bookkeeping is needed for
this case.

---

## 12. Implementation requirements summary

For unambiguous interoperability, an implementation of this format
SHALL satisfy the following, all of which derive from the sections
above and from [REQ-DM-005](../../requirements/domain/REQ-DM-005.md):

1. Bit order within each byte: most significant bit first (§1.1).
2. Byte order for multi-byte primitives: big-endian (§1.2).
3. Signed-integer representation: two's-complement, sign-extended to
   the field width (§1.3, §2.1, §2.2).
4. Variable integers: shortest encoding per type; continuation bit is
   the MSB of each byte; signed flavours use a sign-magnitude leading
   byte; `varsize` magnitudes are capped at `2^31 − 1` (§2.4).
5. Floats: IEEE 754 bit pattern, big-endian, at the declared width
   (§2.3).
6. Strings: `varsize` byte-length prefix, UTF-8 payload (§2.6).
7. Externs / bytes: `varsize` bit-length / byte-length prefix, raw
   payload at the current position (§2.7, §2.8).
8. Structs: source-order field encoding, no tags, no length, no
   implicit padding (§5).
9. Choices: payload-only encoding driven by the selector parameter
   (§6).
10. Unions: `varsize` source-order tag followed by the selected branch
    (§7).
11. Auto-optional: 1-bit presence flag followed by the value when
    present; conditional optional: gated by the schema's `if`
    expression on both sides (§8).
12. Arrays: fixed/length-expression carry no prefix; auto-length
    carries a `varsize` count; implicit-length reads until end of
    stream (§9.1–§9.3). All array lengths are bounded by `2^31 − 1`
    (§9 preamble).
13. Packed arrays: per-packable-field `PackingDescriptor` followed by
    the first element and the deltas (or the unpacked sequence when
    the `isPacked` decision is `0`). Delta width is `maxBitNumber + 1`
    bits signed. Nesting is recursive on the same rule (§9.4–§9.10).
14. Alignment: zero-padding to the next multiple of the declared
    width (§10.1).
15. Offset / indexed offset: implicit byte alignment of the offsetted
    field(s); recorded offset checked by the decoder against the
    actual position (§10.2, §10.3).
16. The bitstream total length is bounded by `2^31 − 1` bits
    (§1.5).

Divergence from any of the above is a defect, in this specification or
in the implementation, never an acceptable state.

---

## See also

- [`schema-language.md`](schema-language.md) — the language whose
  constructs the wire format is parameterised on.
- [`schema-evolution.md`](schema-evolution.md) — the `extend` / `optional`
  evolution semantics summarised in §8.
- [`types-mapping.md`](types-mapping.md) — the schema-type → C-type
  table; this document specifies the wire side, that one specifies the
  in-memory side.
- [REQ-DM-003](../../requirements/domain/REQ-DM-003.md) — canonical
  encoding (a property of the wire format).
- [REQ-DM-004](../../requirements/domain/REQ-DM-004.md) — schema
  language reference (the companion specification).
- [REQ-DM-007](../../requirements/domain/REQ-DM-007.md) — wire-format
  versioning; until divergence is declared, this document tracks
  upstream v2.18.1.
