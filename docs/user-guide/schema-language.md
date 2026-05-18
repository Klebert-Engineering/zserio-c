# Schema language

This page summarises which zserio-language features the C
generator supports and how they map to C. The
[upstream language overview](https://github.com/ndsev/zserio/blob/master/doc/ZserioLanguageOverview.md)
is the normative description of the language itself; this page
covers what's reachable from `zserio_c_gen` and where it differs
from upstream.

## Scope

`zserio_c_gen` accepts the full upstream zserio v2.18.1 language.
Every feature listed below is gated by an xvalidation harness in
`tests/xvalidation/harnesses/` — the C output round-trips
byte-identical against the upstream cpp generator on the same
inputs, in both directions.

If you write a schema that compiles against `zserio` upstream
v2.18.1, it compiles here.

## Primitive types

| zserio | C |
|---|---|
| `int8` / `int16` / `int32` / `int64` | `int8_t` / `int16_t` / `int32_t` / `int64_t` |
| `uint8` / `uint16` / `uint32` / `uint64` | `uint8_t` / `uint16_t` / `uint32_t` / `uint64_t` |
| `bit:N` (N is 1..63 constant) | smallest `uint{8,16,32,64}_t` that fits |
| `int:N` (N is 1..63 constant) | smallest `int{8,16,32,64}_t` that fits |
| `bit<N>` (N is a runtime expression) | `uint64_t` (the runtime width is on-wire only) |
| `int<N>` (N is a runtime expression) | `int64_t` |
| `varint16` / `varint32` / `varint64` / `varint` | matching `intNN_t` |
| `varuint16` / `varuint32` / `varuint64` / `varuint` | matching `uintNN_t` |
| `varsize` | `uint32_t` |
| `bool` | `bool` (from `<stdbool.h>`) |
| `float16` | `float` |
| `float32` | `float` |
| `float64` | `double` |
| `string` | `zserio_string_t` (bytes pointer + length) |
| `extern` | `zserio_extern_t` (bit-aligned blob + bit length) |
| `bytes` | `zserio_bytes_t` (byte buffer + length) |

`zserio_string_t`, `zserio_extern_t`, and `zserio_bytes_t` are
defined in `runtime/include/zserio/types.h`. They carry pointer
plus length plus an internal allocator-ownership flag that
`_free` checks.

## Compound types

### Struct

Fields are written in source order with no per-field tag prefix
on the wire.

```zserio
struct Header
{
    uint32 messageId;
    string label;
    Body   body;
};
```

C emission shape covered in
[Generated code](generated-code.md#field-shapes).

### Choice

A schema-driven variant with a selector expression.

```zserio
choice TaggedValue(uint8 kind) on kind
{
    case 0: uint32 number;
    case 1: string label;
    default: uint8 reserved;
};
```

C emits an anonymous `union { … } u;` for the case storage and
relies on the selector parameter to know which arm is live. The
case arm fields share storage — only one is valid at a time.

`default:` is supported. Cases with no field declaration (just a
label) emit nothing in the union — the case is encoded as a
zero-bit selector match.

### Union

A discriminated variant where the wire format carries the tag
itself.

```zserio
union Message
{
    Heartbeat heartbeat;
    Telemetry telemetry;
    Error     error;
};
```

C emits an explicit `uint32_t tag` field plus an anonymous union
of the per-arm storage. The runtime decodes the tag from a
varsize prefix.

### Enum

Schema enums lower to a C `typedef enum`:

```zserio
enum bit:8 Color
{
    RED   = 0,
    GREEN = 1,
    BLUE  = 2
};
```

```c
typedef enum {
    pkg_COLOR_RED   = 0,
    pkg_COLOR_GREEN = 1,
    pkg_COLOR_BLUE  = 2
} pkg_color_t;
```

The underlying integer type matches the schema-declared width.
The wire format always uses the schema width — the C `enum`'s
chosen storage is an implementation detail of the C compiler.

Decoding an integer that doesn't match any declared enumerator
returns `ZSERIO_ERR_INVALID_ENUM`. The struct field is set to the
out-of-range value (cast through the underlying type) so you can
inspect it.

### Bitmask

```zserio
bitmask bit:8 Permission
{
    READ    = 0x01,
    WRITE   = 0x02,
    EXECUTE = 0x04
};
```

C emits a `typedef` to the matching unsigned integer type plus
`#define`-style item constants:

```c
typedef uint8_t pkg_permission_t;
#define pkg_PERMISSION_READ    0x01u
#define pkg_PERMISSION_WRITE   0x02u
#define pkg_PERMISSION_EXECUTE 0x04u
```

There is no enum tag — bitmasks are bit-orable values, not a
discriminated set.

### Subtype

```zserio
subtype uint32 MessageId;
```

C emits a `typedef`:

```c
typedef uint32_t pkg_message_id_t;
```

Subtypes are transparent at the wire level — they exist for
readability.

## Parameters

Compound types may declare runtime parameters:

```zserio
struct Item(uint32 version, bool extended)
{
    uint16 value;
    uint32 extra if extended;
    uint32 extra2 if version >= 2;
};
```

The parameter values determine the wire format itself:
conditional fields, dynamic-width bit fields, array length
expressions. C emits a `_set_parameters` setter; see
[Generated code](generated-code.md#parameters).

When a parameterised type is a field, the generator chains
parameter propagation automatically. You set parameters on the
outermost type, and inner `_set_parameters` calls are emitted at
the right places in the read / write path.

## Arrays

```zserio
T arr[];        // auto-length, varsize prefix
T arr[N];       // fixed-length (N is a compile-time constant)
T arr[len];     // length is a schema expression
packed T arr[]; // delta-encoded (per-element bit-width chosen at write time)
```

The first two are the common cases. Length-expression arrays are
useful when the length is itself encoded earlier in the same
struct.

`packed` may apply to arrays of scalar types (per-element
delta + zigzag) or arrays of compound types (per-field delta
contexts threaded across all elements). See
[Generated code](generated-code.md#packed-compound-arrays).

## Optional and conditional fields

```zserio
optional uint32 a;      // explicit 1-bit on-wire presence tag
uint32 b if cond;       // presence driven by an expression
```

Both shapes emit a `bool <name>_present` sibling flag. The
on-wire encoding differs: `optional` writes the tag explicitly,
`if cond` evaluates the predicate against already-populated
fields to decide whether to emit the field.

## Extend

`extend` is the v2.x forward-compatibility primitive:

```zserio
struct V2
{
    uint32 value;
    extend uint32 v2Field;
};
```

At read time the field is decoded only if there are bytes
remaining past the current position; otherwise it defaults to
zero. At write time the field is always emitted, byte-aligned.

This lets v1 readers consume v2 buffers (they stop at the v1
schema's end and skip the trailing bytes) and v2 readers consume
v1 buffers (the `extend` field reads short and defaults).

## Templates and instantiate

```zserio
struct Box<T>
{
    T value;
};

instantiate Box<uint32> Uint32Box;
```

`Box` is a template — no code is emitted for it directly. Each
`instantiate` becomes a concrete C type. The generator handles
multi-level template chains (template-of-template-of-…), with
identifier-length truncation via FNV-1a hashing past 160
characters (see [Generated code](generated-code.md#symbol-length-cap)).

NDS Live ships ~600 templated types across ~140 schemas; all
instantiate correctly under our generator. Test corpus:
`conformance/ndslive/` decodes a real 50 KiB NDS Live tile.

## Functions, constraints, offsets

### Function fields

```zserio
struct Header
{
    uint32 length;
    function uint32 sizeBytes() { return length * 4; }
};
```

Schema functions emit C inline functions that take a pointer to
the enclosing struct. They're side-effect-free, used in
length-expressions and offset declarations, and have no presence
on the wire.

### Constraints

```zserio
uint8 value : value > 0 && value < 128;
```

Constraint expressions are evaluated on read; failure returns
`ZSERIO_ERR_CONSTRAINT_VIOLATED`. They're not evaluated on write
— the integrator owns producing valid values.

### Offsets

```zserio
struct Layout
{
    uint32 dataOffset;
@offset dataOffset:
    Data data;
};
```

The `@offset` declaration asserts the next field's byte position.
Mismatch on read returns `ZSERIO_ERR_OFFSET_MISMATCH`. Indexed
offsets are also supported:

```zserio
uint32 offsets[];
@offset offsets[@index]:
Data items[];
```

The runtime patches indexed offsets during write — you don't
populate them yourself.

## What's not supported

The following upstream features are intentionally not on the C
roadmap:

- **SQL extension** — relational metadata layer on top of zserio
  for managing schema-tagged sqlite tables. Out of scope for a
  C99 embedded target.
- **Service / pubsub** — RPC-style service/topic declarations.
  Generated stubs would imply a transport choice that should be
  the integrator's, not ours.

Anything else from the upstream v2.18.1 language is supported.
If you find a gap, open an issue with the schema that hits it.

## Reference corpus

182 schemas in `tests/xvalidation/harnesses/` cover one language
feature each, named by the feature (`auto_optional_bool`,
`packed_auto_array_struct_recursion`, `choice_templated_enum_selector`,
etc.). They're a fast lookup if you want a minimal example of a
specific feature.
