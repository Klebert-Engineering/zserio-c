# Schema language reference

**Status:** normative for zserio-c. Discharges [REQ-DM-004](../../requirements/domain/REQ-DM-004.md).

**Origin:** This document is adapted from upstream zserio's `ZserioLanguageOverview.md` (BSD-3-Clause, © Navigation Data Standard e.v.) at pinned version v2.18.1. The adaptation preserves all normative content; structural and editorial changes are zserio-c project conventions.

**Audience:** independent implementers of a zserio-language parser; schema authors who need precise semantics.

---

## Contents

- [Conventions](#conventions)
- [Lexical structure](#lexical-structure)
  - [Comments](#comments)
  - [Identifiers and keywords](#identifiers-and-keywords)
  - [Literals](#literals)
- [Built-in types](#built-in-types)
  - [Fixed-width integers](#fixed-width-integers)
  - [Bit-field types](#bit-field-types)
  - [Variable-length integers](#variable-length-integers)
  - [Floating-point types](#floating-point-types)
  - [Boolean type](#boolean-type)
  - [String type](#string-type)
  - [Bytes type](#bytes-type)
  - [Extern type](#extern-type)
- [Constants](#constants)
- [Enumeration types](#enumeration-types)
- [Bitmask types](#bitmask-types)
- [Compound types](#compound-types)
  - [Structure](#structure)
  - [Choice](#choice)
  - [Union](#union)
  - [Constraints](#constraints)
  - [Optional fields](#optional-fields)
  - [Extend (forward-compatible fields)](#extend-forward-compatible-fields)
  - [Functions](#functions)
  - [Default values](#default-values)
- [Array types](#array-types)
- [Alignment and offsets](#alignment-and-offsets)
- [Expressions](#expressions)
  - [Operands and types](#operands-and-types)
  - [Unary operators](#unary-operators)
  - [Binary operators](#binary-operators)
  - [Conditional operator](#conditional-operator)
  - [Built-in operators](#built-in-operators)
  - [Operator precedence](#operator-precedence)
  - [Member access](#member-access)
- [Parameterized types](#parameterized-types)
- [Subtypes](#subtypes)
- [Packages and imports](#packages-and-imports)
- [Templates](#templates)
- [Instantiate](#instantiate)
- [Rules](#rules)
- [Documentation comments](#documentation-comments)
- [Annotations](#annotations)
- [Compiler directives](#compiler-directives)
- [Out-of-scope upstream features](#out-of-scope-upstream-features)
- [Conformance and verification](#conformance-and-verification)

---

## Conventions

The grammar is described informally; productions are introduced as
they become relevant. Token names use `lowercase`. Literal keyword
text appears in `code font`. Square brackets `[ ]` indicate optional
elements. Vertical bars `|` separate alternatives.

Code examples use `zserio` fenced blocks. Examples are normative —
each one MUST parse and produce the documented behavior.

Where this document refers to a "schema author" it means the human
writing `.zs` files. "Generator" means a tool that consumes a parsed
schema and emits target-language code; for zserio-c, that tool is
`zserio_c_gen`. "Decoder" and "encoder" refer to the generated code
plus runtime that consumes or produces a bitstream.

This reference is paired with [`wire-format.md`](wire-format.md) for
on-wire encoding details, [`types-mapping.md`](types-mapping.md) for
the schema-to-C type table, and
[`schema-evolution.md`](schema-evolution.md) for the formal
semantics of `extend` and `optional`.

---

## Lexical structure

A source file is a sequence of tokens separated by whitespace and
comments. Whitespace is significant only insofar as it terminates
tokens. Line endings are not significant outside of comments and
single-line documentation.

### Comments

Three comment forms exist. Two are normative for tooling; one is
discarded.

| Syntax | Form | Behavior |
|---|---|---|
| `// … <EOL>` | Single-line | Discarded by the parser; not available to generators. |
| `/* … */` | Multi-line | Discarded by the parser; not available to generators. |
| `/** … */` | Classic doc-comment | Preserved; associated with the immediately following declaration. |
| `/*! … !*/` | Markdown doc-comment | Preserved; same association rule. |

`/**` and `/*!` comments may only be separated from their target
declaration by whitespace. They cannot float free.

See [Documentation comments](#documentation-comments) for the tag
grammar of classic doc-comments.

### Identifiers and keywords

An identifier matches `[A-Za-z_][A-Za-z0-9_]*`. Identifiers are
case-sensitive. The following words are reserved as keywords and
MUST NOT be used as identifiers:

```
align       bit         bitmask     bool        bytes
case        choice      const       default     enum
explicit    extend      extern      float16     float32
float64     function    if          implicit    import
instantiate int         int8        int16       int32
int64       lengthof    numbits     on          optional
package     packed      pubsub      publish     return
rule        rule_group  service     sql         sql_database
sql_table   sql_virtual sql_without_rowid       string
struct      subtype     subscribe   topic       uint8
uint16      uint32      uint64      union       using
valueof     varint      varint16    varint32    varint64
varsize     varuint     varuint16   varuint32   varuint64
zserio_compatibility_version        true        false
isset
```

The boolean literals `true` and `false` are keywords syntactically;
see [Literals](#literals).

Reserved keywords for out-of-scope features (`service`, `pubsub`,
`publish`, `subscribe`, `topic`, `sql`, `sql_table`, `sql_database`,
`sql_virtual`, `sql_without_rowid`, `using`, `explicit`) are
recognized by the lexer so that schemas using them produce
informative diagnostics rather than identifier-parse errors. See
[Out-of-scope upstream features](#out-of-scope-upstream-features).

### Literals

#### Boolean literals

`true` and `false`. Always of type `bool`.

#### Integer literals

Integer literals may be written in four bases. All bases accept an
optional leading sign (`+` or `-`).

| Base | Prefix / suffix | Examples |
|---|---|---|
| Decimal | none | `100`, `4711`, `-3`, `+2` |
| Hexadecimal | leading `0x` or `0X` (case-insensitive) | `0xCAFEBABE`, `0XFF`, `-0xEF` |
| Octal | leading `0` followed by octal digits | `044`, `0377`, `-010` |
| Binary | trailing `b` or `B` | `111b`, `001B`, `-1010b` |

The literal value is interpreted as an integer of the smallest
type that can hold it. Where a literal is assigned to a typed
target (a `const`, an enum item, a default value), the value must
fit the target type or the schema is rejected.

#### Floating-point literals

A floating-point literal contains either a decimal point, an
exponent (`e` or `E`), or both, optionally followed by a precision
suffix.

| Suffix | Type |
|---|---|
| `f` or `F` | `float16` or `float32` (context-dependent) |
| none | `float64` |

The `f` suffix is shared between `float16` and `float32`; the
parser disambiguates by the declared target type at the point of
use. Examples: `3.14f`, `31.4e-1f`, `314e-2f`, `0.314e+1`,
`0.0314e2`.

#### String literals

A string literal is enclosed in double quotes and contains UTF-8
text. The following escape sequences are recognized:

| Escape | Meaning |
|---|---|
| `\u####` | 16-bit Unicode code point; `####` are four hex digits |
| `\x##` | 8-bit character; `##` are two hex digits |
| `\0oo`, `\0coo` | 8-bit character in octal; `o` is `0..7`, leading `c` is `0..3` |
| `\r` | carriage return (0x0D) |
| `\n` | line feed (0x0A) |
| `\f` | form feed (0x0C) |
| `\t` | horizontal tab (0x09) |
| `\\` | backslash (0x5C) |
| `\"` | double quote (0x22) |

A signing prefix is not permitted on a string literal.

---

## Built-in types

Built-in types are the atomic building blocks of the type system.
Every compound type is built from them.

The C-side representation of each built-in is fixed by the
generator; see [`types-mapping.md`](types-mapping.md) for the
authoritative table.

### Fixed-width integers

| Schema type | Width | Signed | Range |
|---|---|---|---|
| `int8` | 8 bits | yes | -128 .. 127 |
| `int16` | 16 bits | yes | -32 768 .. 32 767 |
| `int32` | 32 bits | yes | -2 147 483 648 .. 2 147 483 647 |
| `int64` | 64 bits | yes | -2^63 .. 2^63 - 1 |
| `uint8` | 8 bits | no | 0 .. 255 |
| `uint16` | 16 bits | no | 0 .. 65 535 |
| `uint32` | 32 bits | no | 0 .. 2^32 - 1 |
| `uint64` | 64 bits | no | 0 .. 2^64 - 1 |

These are encoded in big-endian byte order, written bit-by-bit
from the most-significant bit to the least-significant. See
[`wire-format.md`](wire-format.md) for the bit-stream model.

### Bit-field types

A bit-field has a width in bits, declared either at schema-time as
a literal or at runtime as an expression. The width is bounded by
`[1, 64]` bits.

| Form | Width source | Signed | C binding hint |
|---|---|---|---|
| `bit:N` | literal `N` in `1..63` | no | smallest unsigned int that fits |
| `int:N` | literal `N` in `1..64` | yes | smallest signed int that fits |
| `bit<expr>` | runtime expression | no | `uint64_t` |
| `int<expr>` | runtime expression | yes | `int64_t` |

`bit:16` and `uint16` are equivalent on the wire and in the type
system; `int:16` and `int16` likewise. The width `N` is the number
of bits actually emitted to the stream.

For the dynamic forms, the expression is evaluated against the
enclosing compound's parameters and already-decoded fields; the
result MUST land in `1..64` or decoding fails.

The value range of `bit:N` is `0 .. 2^N - 1`. The value range of
`int:N` is `-2^(N-1) .. 2^(N-1) - 1`.

### Variable-length integers

Variable-length integers store integer values using a
value-dependent byte count. Smaller magnitudes use fewer bytes.

| Type | Range | Max bytes |
|---|---|---|
| `varint16` | -16 383 .. 16 383 (`±(2^14 - 1)`) | 2 |
| `varint32` | ±(2^28 - 1) | 4 |
| `varint64` | ±(2^56 - 1) | 8 |
| `varint`   | full `int64` range | 9 |
| `varuint16` | 0 .. 2^15 - 1 | 2 |
| `varuint32` | 0 .. 2^29 - 1 | 4 |
| `varuint64` | 0 .. 2^57 - 1 | 8 |
| `varuint`   | 0 .. 2^64 - 1 | 9 |
| `varsize`   | 0 .. 2^31 - 1 | 5 |

`varint` and `varuint` can encode the full range of `int64` and
`uint64` respectively. `varsize` is the canonical type used by the
language for array-length and string-length prefixes; its range is
capped at 31 bits to remain interoperable with target languages
whose array-length type is a 32-bit signed integer.

`varsize` was introduced in upstream `2.0.0`; the others predate
schema language v2.

The exact encoding of each variable-length form is defined in
[`wire-format.md`](wire-format.md).

### Floating-point types

Floating-point types follow IEEE 754 layout.

| Type | Sign bit | Exponent bits | Mantissa bits |
|---|---|---|---|
| `float16` | 1 | 5 | 10 |
| `float32` | 1 | 8 | 23 |
| `float64` | 1 | 11 | 52 |

The C binding represents `float16` as `float` (the runtime expands
on read and narrows on write); `float32` as `float`; `float64` as
`double`. Endianness on the wire is big-endian.

### Boolean type

`bool`. Encoded as a single bit. Values: `true`, `false`.

```zserio
struct TestStructure
{
    bool hasValue;
    int16 value if hasValue == true;
};
```

### String type

`string`. A UTF-8 byte sequence prefixed by its byte length encoded
as `varsize`. The length-prefix is the byte count, not the
code-point count.

```zserio
struct TestStructure
{
    string textField;
};
```

The C binding represents `string` as `zserio_string_t` (pointer
plus length plus ownership flag). See
[`types-mapping.md`](types-mapping.md).

### Bytes type

`bytes`. A raw byte sequence prefixed by its byte length encoded
as `varsize`.

```zserio
struct StructureWithBytesField
{
    string text;
    bytes  data;
};
```

The C binding represents `bytes` as `zserio_bytes_t`. Available
since upstream `2.9.0`.

### Extern type

`extern`. An opaque bit sequence whose internal structure is
unknown to zserio. Encoded as a bit-length prefix (`varsize`)
followed by exactly that many raw bits.

```zserio
struct StructureWithExternalField
{
    bit:3   numberA;
    extern  blob;
    bit:7   numberC;
};
```

Unlike `bytes`, the length is in bits, not bytes — `extern` is the
right choice when the payload boundary is not byte-aligned. The C
binding represents `extern` as `zserio_extern_t` (bit-buffer
pointer plus bit count plus ownership flag). Available since
upstream `1.4.0`.

---

## Constants

A constant is an immutable named value of a built-in scalar type.

```
const built-in-type NAME = literal ;
```

```zserio
const bit:1 FALSE = 0;
const bit:1 TRUE  = 1;
const int16 i     = 1234;
const int32 j     = -5678;
```

Constants live in package scope and may be referenced from any
expression in any type defined in the same package, or from
expressions in packages that import the defining one. They have no
on-wire footprint.

---

## Enumeration types

An enumeration type has an integer or bit-field base type and a
list of named items. Each item has a value either assigned
explicitly or derived from the previous item by incrementing by 1.
The first item, if unassigned, takes the value 0.

```zserio
enum bit:3 Color
{
    NONE  = 000b,
    RED   = 010b,
    BLUE,
    BLACK = 111b
};
```

In the above example, `BLUE` has the value 3. When decoding a
`Color`, the decoder reads three bits and reports an error if the
value is not one of 0, 2, 3, or 7.

An enumeration introduces its own lexical scope. Item names must
be unique within the enumeration, but may be reused in other
contexts. Outside the enumeration's body, an item must be
qualified by the enum name: `Color.NONE`.

The integer value of an enumeration expression is obtained with
the [`valueof`](#valueof-operator) operator.

Enumeration items may carry the
[`@deprecated`](#deprecated-annotation) or
[`@removed`](#removed-annotation) annotation.

The C binding lowers enums to `typedef enum`; see
[generated-code](../user-guide/generated-code.md#naming). An
out-of-range enum value at decode time returns
`ZSERIO_ERR_INVALID_ENUM`; the field is populated with the raw
integer value so the integrator can inspect it.

---

## Bitmask types

A bitmask type has an unsigned-integer or unsigned-bit-field base
type and a list of named values. A value without an explicit
assignment is auto-assigned to the next unused power of two. If
the first value is unassigned, it is assigned 1.

```zserio
bitmask uint8 Permission
{
    EXECUTABLE,
    READABLE = 0x02,
    WRITABLE
};
```

In the example above, `EXECUTABLE` auto-assigns to 1, `READABLE`
is manually 2, and `WRITABLE` takes the next unused bit, 4.

A bitmask introduces its own lexical scope. Value names must be
unique within the bitmask. Outside the body, a value must be
qualified by the bitmask name: `Permission.WRITABLE`. The only
exception is inside [`isset`](#isset-operator) where the qualifier
may be omitted.

Bitmasks support the bitwise operations `&`, `|`, `^`, `~` and
relational comparisons against other values of the same bitmask.

```zserio
bitmask bit:2 Availability
{
    VERSION_NUMBER,
    VERSION_STRING
};

struct Version(Availability availability)
{
    uint32 versionNumber if (availability & Availability.VERSION_NUMBER)
                            == Availability.VERSION_NUMBER;
    string versionString if (availability & Availability.VERSION_STRING)
                            == Availability.VERSION_STRING;
};
```

A bitmask may include a value explicitly assigned to 0 to
represent an empty mask. This is a common idiom (`NONE = 0`).

The integer value of a bitmask expression is obtained with the
[`valueof`](#valueof-operator) operator. Available since upstream
`2.0.0`.

---

## Compound types

A compound type aggregates fields. Three forms exist: structure,
choice, union. All three share the field-declaration syntax for
their member list and may carry parameters
([Parameterized types](#parameterized-types)).

### Structure

A structure is the concatenation of its fields in source order.
There is no padding or alignment between fields (alignment is
opt-in; see [Alignment](#alignment-and-offsets)). There is no
length prefix and no per-field tag on the wire.

```zserio
struct MyStructure
{
    bit:4 a;
    uint8 b;
    bit:4 c;
};
```

A field declaration has the shape:

```
[ optional ] [ extend ] type name [ array-suffix ]
    [ = default-value ]
    [ if condition ]
    [ : constraint ]
    ;
```

The order of the optional clauses is fixed as shown. `optional`
and `if` are mutually exclusive (the `optional` keyword implies
its own presence bit; `if` is an explicit predicate).

### Choice

A choice type carries a selector expression and a list of `case`
arms, each labeled by one or more literal values. At read time,
the selector is evaluated and the matching arm is decoded; at
write time, the same selection happens and the matching arm is
emitted. The selector itself is NOT written to the bitstream —
it must already be known to the decoder, either as a parameter or
as a field of an enclosing type.

```zserio
choice VarCoordXY(uint8 width) on width
{
    case  8: bit:8  coord8;
    case 16: bit:16 coord16;
    case 24: bit:24 coord24;
    case 32: bit:32 coord32;
};
```

A single arm may carry multiple case labels:

```zserio
choice AreaAttributes(AreaType type) on type
{
    case AreaType.COUNTRY:   // prefix is optional for enum / bitmask selectors
    case STATE:
    case CITY:
        RegionAttributes regionAttr;
    case MAP:
        ;                    // empty arm
    case ROAD:
        RoadAttributes roadAttr;
    default:
        DefaultAttributes defaultAttr;
};
```

Properties of choice arms:

- An arm may be empty (semicolon only). Empty arms encode zero
  bits and decode to a populated struct with no arm-specific
  field set.
- A choice may have a `default:` arm. If no `default` arm is
  declared and the selector matches no case label, decoding fails.
- When the selector type is an enum or bitmask, the case-label
  literals may omit the type prefix.
- When the selector type is a template parameter, the type prefix
  on case-label literals MUST be present, even if every
  instantiation uses an enum or bitmask.

The C binding emits an anonymous `union { … } u;` for arm
storage. See [Choice](../user-guide/generated-code.md#choices-and-unions)
in generated-code for layout details.

### Union

A union is a discriminated variant where the wire format carries
the discriminator (the "tag") itself. The tag is encoded as a
`varsize` value preceding the arm-specific bytes. Reading recovers
the tag, then the arm.

```zserio
union SimpleUnion
{
    uint8  value8;
    uint16 value16;
};
```

In `SimpleUnion`, two branches share storage; the tag value `0`
selects `value8` and `1` selects `value16`. Tag values are assigned
in source order.

Use a choice when the selector is already in scope (as a parameter
or a sibling field); use a union when the selection must be
self-describing on the wire.

### Constraints

Any field may carry a constraint — a boolean expression that the
decoder evaluates after reading the field. If the constraint
evaluates to false, decoding fails with a constraint-violation
error.

```zserio
struct GraphicControlExtension
{
    uint8 byteCount       : byteCount == 4;
    uint8 blockTerminator : blockTerminator == 0;
};

choice ChoiceConstraints(bool selector) on selector
{
    case true:
        uint8  value8  : value8 != 0;
    case false:
        uint16 value16 : value16 > 255;
};
```

The constraint expression may refer to the field itself, to
earlier fields, and to compound parameters. A constraint may
reference a sibling boolean directly:

```zserio
struct TestStructure
{
    bool   isValueValid;
    uint16 value : isValueValid;
};
```

Constraints are evaluated on read only. The encoder does not
re-validate constraints at write time; the integrator is
responsible for populating valid values.

### Optional fields

A structure field may be conditionally present. Two syntaxes exist:

```zserio
struct ItemCount
{
    uint8  count8;
    uint16 count16 if count8 == 0xFF;     // expression-driven
};

struct Container
{
    int32          nonOptionalInt;
    optional int32 autoOptionalInt;       // explicit 1-bit tag
};
```

The two forms differ in how presence is determined:

| Form | Presence source | On-wire tag |
|---|---|---|
| `T x if cond` | `cond` evaluated against parameters and earlier fields | no |
| `optional T x` | A 1-bit presence flag written before the field | yes (1 bit) |

The `optional` keyword cannot coexist with `if`: `optional T x if
…` is rejected. The `if` form requires the predicate to be
evaluable from already-decoded context (the decoder needs to know
whether to read the field before reading it). The `optional` form
imposes no such dependency.

On the C side, both forms emit a sibling `bool <name>_present`
flag; see
[generated-code](../user-guide/generated-code.md#optional-and-conditional).
For `optional T`, the flag is the wire-format presence bit. For
the `if` form, the flag is set when the predicate would evaluate
true given the populated fields.

### Extend (forward-compatible fields)

Top-level structure fields may be marked `extend`. An `extend`
field is a forward-compatibility primitive: a v2 decoder can read
v1 bytes (the trailing `extend` reads default to zero) and a v1
decoder can read v2 bytes (the trailing `extend` bytes are simply
left at the buffer boundary).

```zserio
struct TopLevelBlob
{
    uint32 data[];                    // last field in version 1
    extend ExtendedData extendedData; // added in version 2
    extend bool         additionalFlag; // added in version 3
};

struct ExtendedData
{
    varint additionalData[];
};
```

Rules:

- `extend` fields may appear only at the end of a top-level
  structure.
- Once a structure has any `extend` field, every subsequent field
  in source order MUST also be `extend`.
- An `extend` field has no on-wire tag of its own. At read time,
  if the bitstream has bytes remaining past the current position,
  the field is decoded; otherwise it stays at its
  default-initialized value.
- At write time, an `extend` field is always emitted,
  byte-aligned to the start of the field.

It is the schema author's responsibility never to reorder existing
non-`extend` fields when adding `extend` fields — doing so breaks
forward compatibility.

Available since upstream `2.12.0`. See
[`schema-evolution.md`](schema-evolution.md) for the full
evolution model.

### Functions

A compound type may declare functions — read-only expressions
that take no arguments and return a value computed from the
enclosing instance.

```zserio
struct ItemCount
{
    uint8   count8;
    uint16  count16 if count8 == 0xFF;

    function uint16 getValue()
    {
        return (count8 == 0xFF) ? count16 : count8;
    }
};
```

The function's return type must be an integer built-in or a
compound type. The function body must consist of a single `return`
statement whose expression matches the return type.

A function may reference compound parameters:

```zserio
struct MetresConverter(uint16 metres)
{
    uint16 valueA;

    function uint16 toCentimetres()
    {
        return 100 * metres;
    }
};
```

Functions are expression-only — there is no looping, branching, or
mutation. Their intended use is to expose derived values for use
in length-expressions, offset declarations, and constraints.

### Default values

A structure field of scalar, enum, bitmask, or string type may
declare a default value, written after the field declaration with
`=`:

```zserio
enum uint8 BasicColor
{
    BLACK,
    WHITE,
    RED
};

struct StructureDefaultValues
{
    bool       boolValue   = true;
    bit:4      bit4Value   = 0x0F if boolValue == true;
    int16      int16Value  = 0x0BEE;
    float16    float16Value = 1.23f;
    float32    float32Value = 1.234f;
    float64    float64Value = 1.2345;
    string     stringValue = "string";
    BasicColor enumValue   = BasicColor.BLACK;
};
```

Default values apply at encode time when the integrator has not
populated the field. They are written to the stream. They are NOT
applied at decode time — there is no on-wire signaling for "use
the default", because the stream has no wire-format slot for "this
field is absent". This is a deliberate departure from formats like
protobuf.

Arrays and compound-type fields cannot carry default values.

---

## Array types

An array is a sequence of elements of the same type. The element
type may be any other type except an array (two-dimensional arrays
must be expressed by wrapping the inner array in a structure).

Array elements have zero-based indices `0..n-1`. Individual
elements may be referenced in expressions with the usual `[i]`
syntax: `list[2]` is the third element of `list`.

Six array forms exist:

| Form | Length source | On-wire length prefix |
|---|---|---|
| `T arr[N]` | literal `N` (compile-time constant) | none |
| `T arr[expr]` | `expr` evaluated against parameters and earlier fields | none |
| `T arr[]` | self-describing (varsize prefix) | yes |
| `implicit T arr[]` | bitstream remainder | none (deprecated; see below) |
| `packed T arr[N]` / `packed T arr[expr]` / `packed T arr[]` | as above | as above; payload is delta-compressed |

Examples:

```zserio
struct ArrayExample
{
    uint8   header[256];      // fixed length
    int16   numItems;
    Element list[numItems];   // length is an expression
};

struct AutoArray
{
    Element list[];           // length is self-describing
};
```

### Implicit length arrays

`implicit T arr[]` consumes bytes until the bitstream ends. It
must be the final field of its containing type, and the payload
must be the entire bitstream tail. Implicit arrays of compound
elements are NOT permitted.

```zserio
struct ImplicitArray
{
    implicit int16 list[];
};
```

`implicit` is deprecated as of upstream `2.4.0`. New schemas
should use `T arr[]` instead. zserio-c continues to accept
`implicit` for compatibility with existing schemas.

The length of an `implicit` array is recoverable through
[`lengthof`](#lengthof-operator).

### Packed arrays

A `packed` array applies delta compression to its elements. The
packing algorithm is per-element delta encoding with a per-array
maximum-delta bit-width.

Packable element types:

- All integer built-ins (`uint8`..`uint64`, `int8`..`int64`,
  `bit:N`, `int:N`, `varint*`, `varuint*`, `varsize`)
- Enumeration types
- Bitmask types
- Compound types containing at least one packable field
  (recursively)
- All union types (the union tag is always a packable integer)

Floats, strings, bytes, extern, and bool are NOT packable. A
compound type containing only non-packable scalar fields is itself
non-packable; the schema MUST NOT mark an array of such a compound
as `packed`.

```zserio
struct PackedArrays
{
    packed uint8   header[256];
    packed int16   items[];
    uint16         numItems;
    packed Element elements[numItems];
};
```

Restrictions:

- Packed `implicit` arrays are forbidden.
- A field used as an offset cannot be packed (the wire-format
  positions of packed elements are not statically known at the
  point of offset resolution).

Available since upstream `2.4.0`. The exact wire format of packed
arrays is in [`wire-format.md`](wire-format.md). The C binding for
packed compound arrays is in
[generated-code](../user-guide/generated-code.md#packed-compound-arrays).

---

## Alignment and offsets

The wire format is bit-packed by default. Alignment and offsets
are the two opt-in mechanisms for imposing byte (or coarser)
alignment on selected fields.

### Alignment

The `align(N)` modifier inserts 0..N-1 padding bits before the
next field so that the bit-offset from the start of the bitstream
is divisible by N. `N` may be any positive integer literal.

```zserio
struct AlignmentExample
{
    bit:11 a;

align(32):
    uint32 b;
};
```

The size of `AlignmentExample` is 64 bits. Without the alignment
modifier, it would be 43 bits.

If the field that follows an `align` directive is optional, the
alignment is also conditional:

```zserio
struct Example
{
    bool    hasOptional;

align(32):
    int32   myOptionalField if hasOptional == true;
    int32   myField;
};
```

When `hasOptional == false`, the `align(32)` is not applied and
the total size is 33 bits.

### Offsets

The name of an integer-typed field may be used as a byte offset
declaration for a subsequent field:

```zserio
struct Tile
{
    TileHeader  header;
    uint32      stringOffset;
    uint16      numFeatures;

stringOffset:
    StringTable stringTable;
};
```

The declaration `stringOffset:` asserts that the byte offset of
`stringTable` from the start of the bitstream equals the runtime
value of `stringOffset`. On read, the decoder verifies the
assertion. On write, the encoder patches the offset field to the
correct value after writing the target.

Because offsets are byte offsets, the target field must be
byte-aligned. The compiler inserts an automatic `align(8)` before
the target when necessary:

```zserio
struct Tile
{
    TileHeader  header;
    uint32      stringOffset;
    uint16      numBits;
    bit:1       bits[numBits];

stringOffset: // implies align(8)
    StringTable stringTable;
};
```

If the target field is optional, the offset is also conditional:

```zserio
struct Example
{
    uint32 byteOffset;
    bool   hasOptional;

byteOffset:
    int32 myOptionalField if hasOptional == true;

    int32 myField;
};
```

When `hasOptional == false`, the offset is not checked and the
struct ends after `myField`.

Constraints on offset use:

- A field declared as an offset cannot appear in any other
  [expression](#expressions).
- A field may be used as an offset at most once.

### Indexed offsets

When every element of an array should carry its own byte offset, a
special form is used:

```zserio
struct IndexedInt32Array
{
    uint32 offsets[10];
    bit:1  spacer;

offsets[@index]:
    int32  data[10];
};
```

`@index` denotes the current index of `data` during iteration.
The declaration asserts that `offsets[i]` is the byte offset of
`data[i]` from the start of the bitstream. As with scalar offsets,
each element is automatically byte-aligned:

```zserio
struct IndexedBit5Array
{
    uint32 offsets[2];
    bit:1  spacer;

offsets[@index]: // implies align(8) before each data[i]
    bit:5  data[2];
};
```

The same restrictions apply: an indexed offset field cannot be
used in any other expression, and each field may be used as an
indexed offset at most once.

---

## Expressions

Expressions appear in conditional fields, default values, array
length specifications, parameter argument lists, function bodies,
constraints, and case labels.

The semantics follow Java conventions except as noted. Zserio
expressions do NOT include the Java operators `++`, `--`, `>>>`,
or `instanceof`.

### Operands and types

An operand is one of:

- An integer, floating-point, boolean, or string literal.
- A reference to a constant.
- A reference to a field of the enclosing compound (or any
  enclosing compound, by member access).
- A reference to a parameter of the enclosing compound.
- A reference to an enum item or bitmask value.
- A call to a function declared on a compound type.
- A call to a built-in operator (`lengthof`, `valueof`, `numbits`,
  `isset`).

Integer operands undergo standard arithmetic promotion. A bit
field, varint, or fixed-width integer participates in expressions
as a 64-bit signed or unsigned value depending on its declared
signedness. Mixed-sign operations are permitted; the result type
follows the wider operand.

Equality (`==`, `!=`) applies to any two operands of compatible
type. Relational operators (`<`, `<=`, `>`, `>=`) apply to integer
and floating-point operands.

### Unary operators

| Operator | Operand type | Result |
|---|---|---|
| `!` | bool | bool — logical negation |
| `+` | integer or float | same — identity |
| `-` | integer or float | same — arithmetic negation |
| `~` | integer or bitmask | same — bitwise complement |

### Binary operators

#### Arithmetic

| Operator | Operands | Result |
|---|---|---|
| `+` `-` `*` `/` `%` | integer or float | numeric |
| `<<` `>>` | integer | integer (logical/arithmetic per signedness) |

Division by zero in a schema expression is a schema error at
compile time if both operands are constant; otherwise it is a
runtime error in the decoder.

#### Relational

`==` `!=` apply to any pair of compatible-type operands and yield
`bool`. `<` `<=` `>` `>=` apply to integer or floating-point
operands and yield `bool`.

#### Logical

`&&` `||` apply to `bool` operands and yield `bool`. They are
short-circuit-evaluated: `a && b` does not evaluate `b` if `a` is
false; `a || b` does not evaluate `b` if `a` is true.

#### Bitwise

`&` `|` `^` apply to integer and bitmask operands. For bitmasks
they yield a bitmask of the same type; for integers they yield an
integer.

#### Postfix

| Operator | Form | Meaning |
|---|---|---|
| `[]` | `arr[i]` | array element |
| `()` | `f(a, b)` | parameterized-type instantiation or function call |
| `.` | `obj.field` | member access |

### Conditional operator

`a ? b : c` evaluates `a` (which MUST be `bool`); if true, the
expression yields `b`, otherwise `c`. The types of `b` and `c`
must be compatible. The ternary operator is right-associative.

### Built-in operators

#### `lengthof` operator

`lengthof(x)` returns the actual length of an array, string, or
bytes value as an integer.

- For arrays: the element count.
- For strings: the number of UTF-8 bytes (not code points).
  `lengthof("€") == 3`.
- For bytes: the byte count.

`lengthof` is the only way to refer to the length of an auto-length
or implicit array.

```zserio
struct LengthOfOperator
{
    uint8 autoArray[];

    function uint32 getLengthOfAutoArray()
    {
        return lengthof(autoArray);
    }
};
```

#### `valueof` operator

`valueof(x)` returns the integer value of an enumeration or
bitmask expression.

```zserio
enum uint8 Color
{
    WHITE = 1,
    BLACK = 2
};

struct ValueOfOperator
{
    Color color;

    function uint8 getValueOfColor()
    {
        return valueof(color);
    }
};
```

#### `numbits` operator

`numbits(n)` returns the minimum number of bits required to encode
`n` distinct values. The result type is `uint8`.

| `n` | `numbits(n)` |
|---|---|
| 0 | 0 |
| 1 | 1 |
| 2 | 1 |
| 3 | 2 |
| 4 | 2 |
| 8 | 3 |
| 16 | 4 |

The operand must be a non-negative integer expression.

```zserio
struct NumBitsOperator
{
    uint8 value8;

    function uint8 getNumBits8()
    {
        return numbits(value8);
    }
};
```

#### `isset` operator

`isset(mask, value)` returns `true` if `mask` contains `value`,
i.e. if `(mask & value) == value`. Both operands MUST be of the
same bitmask type.

```zserio
bitmask uint8 TestBitmask
{
    INT,
    STRING
};

struct IsSetOperator
{
    TestBitmask testBitmask;

    function bool hasInt()    { return isset(testBitmask, INT); }
    function bool hasString() { return isset(testBitmask, TestBitmask.STRING); }
    function bool hasBoth()   { return isset(testBitmask, INT | STRING); }
};
```

The bitmask-value type prefix may be omitted inside `isset` when
the type can be inferred from the first argument. If the first
argument's type is a template parameter, the type prefix MUST be
present.

The above is semantically equivalent to:

```zserio
function bool hasInt()    { return (testBitmask & TestBitmask.INT) == TestBitmask.INT; }
function bool hasString() { return (testBitmask & TestBitmask.STRING) == TestBitmask.STRING; }
function bool hasBoth()   {
    return (testBitmask & (TestBitmask.INT | TestBitmask.STRING))
        == (TestBitmask.INT | TestBitmask.STRING);
}
```

Available since upstream `2.8.0`.

### Operator precedence

Operators are listed from highest precedence to lowest. Operators
on the same line have equal precedence and are left-associative,
except the conditional `? :`, which is right-associative.

| Precedence | Operators |
|---|---|
| 1 (highest) | `()` `[]` `.` |
| 2 | `lengthof` `valueof` `numbits` |
| 3 | unary `+` `-` `~` `!` |
| 4 | `*` `/` `%` |
| 5 | `+` `-` (binary) |
| 6 | `<<` `>>` |
| 7 | `<` `>` `<=` `>=` |
| 8 | `==` `!=` |
| 9 | `&` |
| 10 | `^` |
| 11 | `|` |
| 12 | `&&` |
| 13 | `||` |
| 14 (lowest) | `? :` |

### Member access

The dot operator accesses a member of a compound value. `f.m` is
well-formed if:

- `f` is a field of compound type `T` in the enclosing scope.
- `T` has a member named `m`.

The value of `f.m` is available at any expression evaluation point
where `f` has already been decoded (or, on the write side, has
already been populated by the integrator).

```zserio
struct Header
{
    uint16 version;
    uint16 numSentences;
};

struct Message
{
    Header header;
    string sentences[header.numSentences];
};
```

`Message.sentences` is an array whose length is the
`numSentences` field of the already-decoded `header`.

Member access chains through nested compounds: `outer.inner.field`
is well-formed when `outer` is a compound with member `inner` and
`inner` is a compound with member `field`.

---

## Parameterized types

A compound type definition may carry a parameter list. Parameters
behave like read-only fields whose values are passed at the use
site rather than encoded on the wire.

```zserio
struct Header
{
    uint32 version;
    uint16 numItems;
};

struct Message
{
    Header       header;
    Item(header) items[header.numItems];
};

struct Item(Header header)
{
    uint16 param;
    uint32 extraParam if header.version >= 10;
};
```

A parameter may be of any built-in or compound type.
Compound-typed parameters are passed by reference at the wire-
format level — only the parameter's identity is transferred, not
its contents (its contents already exist in the bitstream because
they belong to an earlier field).

To use a parameterized type as a field, an argument list is
required at the use site, matching the parameter list in order
and type.

When the element type of an array is parameterized, the argument
list may reference the current index using `@index`:

```zserio
struct Database
{
    uint16                  numBlocks;
    BlockHeader             headers[numBlocks];
    Block(headers[@index])  blocks[numBlocks];
};

struct BlockHeader
{
    uint16 numItems;
    uint32 offset;
};

struct Block(BlockHeader header)
{
    header.offset:
    int64 items[header.numItems];
};
```

The expression `headers[@index]` selects the `i`-th `BlockHeader`
when constructing the `i`-th `Block`.

The C binding emits a `_set_parameters` function per parameterized
type and chains the calls automatically when the parameterized
type appears as a field. See
[generated-code](../user-guide/generated-code.md#parameters).

---

## Subtypes

A subtype is a named alias for an existing type. Subtypes are
transparent at the wire-format level — they exist for readability.

```zserio
subtype uint16 BlockIndex;

struct Block
{
    BlockIndex blockIndex;
    uint32     data;
};
```

A subtype may alias any built-in type, compound type, or template
instantiation:

```zserio
struct TestStructure<T>
{
    T value;
};

subtype TestStructure<uint32> TestStructureSubtype;
```

The C binding lowers subtypes to a `typedef`. The aliased type's
on-wire representation is unchanged.

---

## Packages and imports

Every user-defined type belongs to a package. A schema file
declares its package on the first non-comment, non-directive line:

```zserio
package map;

import common.geometry.*;
import common.featuretypes.*;
```

A schema file without an explicit `package` declaration belongs to
the unnamed default package. Use of the default package is
strongly discouraged; the implementation reserves the right to
diagnose default-package usage in future versions.

A package provides a lexical scope for types. Type names must be
unique within a package. The same simple name may appear in
multiple packages; the fully-qualified name (`com.acme.foo.Coordinate`)
disambiguates.

### Import forms

| Form | Meaning |
|---|---|
| `import pkg.*;` | Wildcard: every type in `pkg` becomes resolvable. |
| `import pkg.Type;` | Single-type: only `Type` from `pkg` becomes resolvable. |
| `import pkg.Symbol;` | Single-symbol: a constant, enum item, or other named entity. |

A single-type import has precedence over any wildcard import for
the same simple name. Wildcard imports do not introduce
ambiguity unless two wildcards both define the same name and the
name is referenced unqualified.

Name resolution proceeds in order:

1. If the name is fully qualified (`com.acme.bar.Coordinate`),
   resolve directly to that package.
2. If the name is defined in the current package, use the local
   definition. Imports are not consulted.
3. If a single-type import names the simple name, use it.
4. If exactly one wildcard import provides the name, use it.
5. If no import provides the name, or more than one wildcard
   provides it ambiguously, the schema is rejected.

### Packages and files

Package and file names are coupled. The package `pkg.sub.module`
is expected at the relative path `pkg/sub/module.zs` from a schema
root directory. The parser uses this convention to locate imports.

Cyclic imports between packages are tolerated but the parser
guarantees each file is parsed exactly once.

---

## Templates

A compound type may be parameterized over types using template
parameters in angle brackets. Template parameters are
type-level — they stand in for any type that appears in an
expression context within the template body.

```zserio
struct Field<T>
{
    T value;
};

struct StructTemplatedField
{
    Field<uint32> uint32Field;
};
```

The instantiation `Field<uint32>` is equivalent at the schema
level to a hand-rolled non-templated copy:

```zserio
struct Field_uint32
{
    uint32 value;
};

struct StructTemplatedField
{
    Field_uint32 uint32Field;
};
```

Template instantiation is resolved at compile time. The generator
emits one concrete type per distinct instantiation; no template
machinery survives into the generated C.

### Parameterized templates

A template's compound type may itself be parameterized:

```zserio
struct ParamHolder<T>
{
    T param;
};

struct Parameterized<T>(ParamHolder<T> paramHolder)
{
    string description;
paramHolder.param:
    uint32 id;
};

struct StructTemplatedTypeArgument
{
    ParamHolder<uint32>                paramHolder;
    Parameterized<uint32>(paramHolder) parameterized;
};
```

### Templated template arguments

A template argument may itself be a template instantiation:

```zserio
struct Field<T>
{
    T value;
};

struct Compound<T>
{
    T value;
};

struct StructTemplatedTemplateArgument
{
    Field<Compound<uint32>> compoundField;
};
```

Instantiation chains may be arbitrarily deep. The generator
applies symbol-name truncation via FNV-1a hashing past 160
characters to keep emitted file names within filesystem limits;
see
[generated-code](../user-guide/generated-code.md#symbol-length-cap).

### Template subtypes

A subtype may alias a template instantiation:

```zserio
struct TestStructure<T>
{
    T value;
};

subtype TestStructure<uint32> TestStructureSubtype;
```

### Template scope

Within a template body, the template parameter `T` is treated as
an opaque type. Operations that depend on the concrete identity of
`T` (case-label literal type-prefix omission, `isset` type
inference) are not permitted; the type prefix must be present so
the template body remains well-formed independently of any
specific instantiation.

---

## Instantiate

By default, the generator emits each template instantiation in the
package of the template definition. This ensures that two
instantiations of the same template with the same argument
collapse to a single emitted type, regardless of where the
instantiation is referenced.

The `instantiate` keyword overrides this default, forcing an
instantiation to be emitted in a specified package under a chosen
name.

```zserio
package template_declaration;

struct TestStructure<T>
{
    T value;
};

package template_instantiation;

import template_declaration.*;

instantiate TestStructure<uint32> TestStructure32;
```

The directive declares that:

- `TestStructure<uint32>` is emitted in `template_instantiation`
  rather than `template_declaration`.
- The emitted type is named `TestStructure32`.
- A new schema-level name `TestStructure32` is introduced in
  `template_instantiation`, usable like any other type.

Constraints:

- A given template instantiation may be the target of at most one
  `instantiate` directive within a single package's transitive
  import closure. Duplicate `instantiate` directives for the same
  template argument list (whether locally declared or imported)
  are rejected.
- Two packages that do not share an import path may each
  independently `instantiate` the same template argument list;
  the result is two distinct emitted types. Schema authors should
  use `instantiate` only when necessary, because of this
  duplication risk.

Imported `instantiate` types are visible like any other imported
type. Available since upstream `1.4.0`.

---

## Rules

A `rule_group` collects schema-level semantic rules that cannot be
expressed as constraints. Rules carry an identifier and a
documentation comment; they have no runtime effect — they are
metadata for documentation generators and review tooling.

```zserio
rule_group SchemaRules
{
    /*!
    Description of the semantic rule. Rule descriptions may include
    images, markdown links, and other documentation-style markup.
    !*/
    rule "rule-01";

    /*!
    Another rule.
    !*/
    rule "rule-02";
};
```

The rule identifier is an extended Zserio identifier that may
additionally contain `-`, `:`, and `.`. Rule IDs MUST be unique
across the entire schema set, case-insensitive.

Available since upstream `2.4.0`.

---

## Documentation comments

Two documentation comment forms are preserved by the parser and
made available to generators: classic (`/** … */`) and markdown
(`/*! … !*/`).

Both are associated with the immediately following declaration,
separated by whitespace only.

### Classic doc-comments

The content between `/**` and `*/` is parsed line by line.
Leading whitespace, a sequence of asterisks, and trailing
whitespace are stripped from each line. The remaining text is one
or more paragraphs separated by blank lines, optionally followed
by zero or more tag blocks.

Paragraphs may contain inline HTML tags (`<ul>`, `<li>`, `</br>`)
for documentation rendering.

A tag block begins on its own line with `@` followed by a tag
keyword. Recognized tags:

| Tag | Syntax | Meaning |
|---|---|---|
| `@see` | `@see "alias" Type.field` | Cross-reference to another schema element. The alias and `.field` are optional. The `@see` tag is the only tag that may appear inline within paragraph text. |
| `@param` | `@param name description` | Documents a parameter of a parameterized type. |
| `@todo` | `@todo description` | Future-action marker. |
| `@deprecated` | `@deprecated` | Marks the declaration as deprecated. Generators may emit a warning at the corresponding generated-code element. |

```zserio
/**
 * Traffic flow on roads.
 */
enum bit:2 Direction
{
    /** No traffic flow allowed. */
    NONE,
    /** Traffic allowed from start to end node. */
    POSITIVE,
    /** Traffic allowed from end to start node. */
    NEGATIVE,
    /** Traffic allowed in both directions. */
    BOTH
};
```

### Markdown doc-comments

Markdown doc-comments begin with `/*!` and SHOULD terminate with
`!*/`. A different terminator (`*/`) is accepted but produces a
warning.

The content is treated as raw Markdown. If the entire comment is
uniformly indented relative to its opening `/*!`, the indentation
is stripped from each line.

```zserio
/*!

# Title for this section

The tile contains a number of different elements grouped by
feature classes.

Name        | Description
------------| ----------------------------------------------
Structure1  | This describes Structure1 in detail.
Structure2  | This describes Structure2 in detail.

!*/
```

Markdown doc-comments do not support the `@`-tag grammar.
Cross-references and parameter descriptions go in the markdown
prose.

Available since upstream `2.1.0`.

---

## Annotations

Annotations attach declarative metadata to a schema element. They
have no on-wire effect; generators may use them to modify emitted
code. An annotation begins with `@` and precedes the element it
annotates.

Currently defined annotations are listed below. zserio-c parses
every defined annotation but may not act on all of them at code
generation time.

### `@deprecated` annotation

Marks the annotated element as deprecated. The schema remains
valid; generators are encouraged to emit a warning at the
corresponding generated-code element when used by an application.

```zserio
enum uint8 Traffic
{
    NONE = 0,
    @deprecated HEAVY,
    LIGHT,
    MID
};
```

Available since upstream `2.12.0`. Currently supported on
enumeration items only.

### `@removed` annotation

Marks an enumeration item as removed from the schema while
preserving it for backward-compatibility with previously-encoded
buffers. Generators may suppress the item from the public API
while still permitting reads of old buffers that contained it.
Writing such an element via the generated API should be
prevented.

```zserio
enum uint8 Traffic
{
    NONE = 0,
    @removed HEAVY,
    LIGHT,
    MID
};
```

Available since upstream `2.12.0`. Currently supported on
enumeration items only.

---

## Compiler directives

### `zserio_compatibility_version`

A compatibility-version directive declares the minimum zserio
language version against which the schema is intended to be
parseable. The directive MUST be the first command (after
comments) in the source file.

```zserio
zserio_compatibility_version("2.4.2");

package compatibility;
```

When the directive is present, the parser verifies that every
feature used by the schema was available in the declared version,
and rejects the schema if not.

Available since upstream `2.5.0`.

---

## Out-of-scope upstream features

The following upstream zserio features are intentionally not
supported by zserio-c. See
[REQ-DM-001](../../requirements/domain/REQ-DM-001.md) for the
scope rationale.

| Feature | Upstream keywords | Why excluded |
|---|---|---|
| SQL extension | `sql_table`, `sql_database`, `sql_virtual`, `sql_without_rowid`, `sql`, `using`, `explicit` (in the SQL-explicit-parameter sense) | Relational metadata layer for SQLite-backed schemas. Outside the C99 embedded-target scope. |
| Service / RPC | `service` | RPC stubs would bind to a specific transport. That choice belongs to the integrator. |
| Pubsub | `pubsub`, `publish`, `subscribe`, `topic` | Same reasoning as service. |
| JSON encoding | (no schema syntax) | Upstream generators support JSON dump/load on generated types. Not in scope for the C target. |

The parser recognizes the reserved keywords and produces an
informative diagnostic when an out-of-scope feature is used. The
C generator never emits anything for them.

These omissions do not affect the wire format. A schema that
restricts itself to the in-scope features parses and generates
identically here and against upstream zserio v2.18.1.

---

## Conformance and verification

zserio-c targets schema-language compatibility with upstream
zserio v2.18.1. Schema-feature coverage is verified by the
cross-validation harness at `tests/xvalidation/harnesses/`: each
of the 182 harnesses exercises one or more language features and
the generated C round-trips byte-identical against upstream-cpp
output on the same schema.

[REQ-DM-004](../../requirements/domain/REQ-DM-004.md) mandates
that every accepted language feature be exercised by at least one
xvalidation harness. The harness naming convention is
feature-descriptive: `auto_optional_bool`,
`packed_auto_array_struct_recursion`,
`choice_templated_enum_selector`, and so on.

For wire-level details of how each language construct encodes,
see [`wire-format.md`](wire-format.md). For the schema-to-C type
table, see [`types-mapping.md`](types-mapping.md). For the formal
semantics of evolution-relevant constructs (`extend`, `optional`),
see [`schema-evolution.md`](schema-evolution.md).
