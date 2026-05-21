# Types mapping — schema → C

**Status:** normative for zserio-c. Discharges
[REQ-FN-009](../../requirements/functional/REQ-FN-009.md).

**Origin:** This document is adapted from upstream zserio's
`ZserioTypesMapping.md` (BSD-3-Clause, © Navigation Data
Standard e.v.) at pinned version v2.18.1, restricted to the C
binding and expanded with zserio-c-specific runtime types.

**Audience:** schema authors and integrators who need to know
the exact C representation of any schema type.

---

This document is normative for the schema-to-C type mapping. The
generator's emission is required to match this table for every
field and every type. A schema construct whose mapping is not
listed here has no defined C representation.

## Base types

| Schema type | C type | Notes |
|---|---|---|
| `int8` | `int8_t` | from `<stdint.h>` |
| `int16` | `int16_t` | |
| `int32` | `int32_t` | |
| `int64` | `int64_t` | |
| `uint8` | `uint8_t` | |
| `uint16` | `uint16_t` | |
| `uint32` | `uint32_t` | |
| `uint64` | `uint64_t` | |
| `bit:1` .. `bit:7` | `uint8_t` | width determined by schema; C type is smallest unsigned that fits |
| `bit:8` | `uint8_t` | |
| `bit:9` .. `bit:15` | `uint16_t` | |
| `bit:16` | `uint16_t` | |
| `bit:17` .. `bit:31` | `uint32_t` | |
| `bit:32` | `uint32_t` | |
| `bit:33` .. `bit:63` | `uint64_t` | |
| `bit:64` | `uint64_t` | |
| `int:1` .. `int:8` | `int8_t` | width determined by schema; C type is smallest signed that fits |
| `int:9` .. `int:16` | `int16_t` | |
| `int:17` .. `int:32` | `int32_t` | |
| `int:33` .. `int:64` | `int64_t` | |
| `bit<N>` (dynamic) | `uint64_t` | runtime width on wire; storage is widest unsigned |
| `int<N>` (dynamic) | `int64_t` | runtime width on wire; storage is widest signed |
| `float16` | `float` | encoded width is 16 bits; storage is `float` |
| `float32` | `float` | |
| `float64` | `double` | |
| `varint16` | `int16_t` | |
| `varint32` | `int32_t` | |
| `varint64` | `int64_t` | |
| `varint` | `int64_t` | |
| `varuint16` | `uint16_t` | |
| `varuint32` | `uint32_t` | |
| `varuint64` | `uint64_t` | |
| `varuint` | `uint64_t` | |
| `varsize` | `uint32_t` | wire encoding caps at 2³¹−1; storage is 32-bit unsigned |
| `bool` | `bool` | from `<stdbool.h>` |
| `string` | `zserio_string_t` | see Runtime types below |
| `extern` | `zserio_extern_t` | see Runtime types below |
| `bytes` | `zserio_bytes_t` | see Runtime types below |

## Runtime types (variable-length payload)

Strings, opaque bit blobs (`extern`), and opaque byte blobs
(`bytes`) carry a length plus a pointer. Their runtime
definitions are in `<zserio/types.h>`:

```c
typedef struct {
    char        *bytes;     /* UTF-8 payload */
    size_t       length;    /* byte count */
    bool         owned;     /* true if allocated by _read; false if borrowed */
} zserio_string_t;

typedef struct {
    uint8_t     *data;      /* big-endian, msb-first */
    size_t       bit_length;
    bool         owned;
} zserio_extern_t;

typedef struct {
    uint8_t     *data;
    size_t       length;    /* byte count */
    bool         owned;
} zserio_bytes_t;
```

The `owned` flag distinguishes decoder-allocated payload (which
`_free` releases) from caller-supplied payload (which `_free`
leaves alone). Encoder users typically set `owned = false` and
point at their own buffer; decoded values come back with
`owned = true`.

`bytes` counts whole bytes; `extern` counts bits. This is the
same distinction upstream makes.

## Compound types

| Schema construct | C representation |
|---|---|
| `struct T { ... }` | `typedef struct { ... } pkg_t_t;` — fields lowered per the field-shape rules below |
| `choice T(P p) on p { ... }` | struct containing `P p;` and `union { ... } u;` |
| `union T { ... }` | struct containing `uint32_t tag;` and `union { ... } u;` |
| `enum bit:N E { ... }` | `typedef enum { pkg_E_ITEM = …, … } pkg_e_t;` |
| `bitmask bit:N B { ... }` | `typedef uintN_t pkg_b_t;` plus `#define pkg_B_ITEM …` per item |
| `subtype T1 T2;` | `typedef pkg_t1_t pkg_t2_t;` (transparent at wire level) |
| `instantiate T<...> T2;` | A concrete `typedef struct` named after `T2` (templates emit nothing themselves) |

For the full naming convention and disambiguation rules, see
[`docs/user-guide/generated-code.md`](../user-guide/generated-code.md)
§ Naming.

## Field shapes

| Schema field shape | C representation |
|---|---|
| `T x;` (scalar) | `T x;` |
| `T x;` (compound) | `T x;` (inlined struct) |
| `optional T x;` | `T x;` plus `bool x_present;` |
| `T x if cond;` | `T x;` plus `bool x_present;` |
| `extend T x;` | `T x;` plus `bool x_present;` |
| `T arr[]` (auto-length) | `T *arr;` plus `size_t arr_count;` |
| `T arr[N]` (fixed-length, N is a compile-time constant) | `T arr[N];` (inline storage) |
| `T arr[expr]` (length-expression) | `T *arr;` plus `size_t arr_count;` |
| `packed T arr[]` | same as auto-length; packing happens at read/write time |

For owned-payload fields (strings, dynamic arrays, `bytes`,
`extern`), `_read` allocates through the supplied allocator and
sets the pointer and length; `_free` releases the allocation.

## Identifier construction

The schema → C identifier mapping is documented in:

- [`docs/user-guide/generated-code.md`](../user-guide/generated-code.md)
  § Naming (friendly summary).
- [REQ-FN-009](../../requirements/functional/REQ-FN-009.md)
  (normative requirement).
- [REQ-FN-010](../../requirements/functional/REQ-FN-010.md)
  (identifier-length cap with FNV-1a truncation).

Schema names use camelCase / PascalCase. C identifiers use
snake_case for fields and types; UPPER_SNAKE_CASE for enum and
bitmask items. The conversion is deterministic and total.

## What this document does not cover

- The wire format for each type — see
  [`wire-format.md`](wire-format.md).
- The schema language grammar and semantics — see
  [`schema-language.md`](schema-language.md).
- The runtime lifecycle entry points (`_init`, `_read`, etc.) —
  see [`docs/user-guide/generated-code.md`](../user-guide/generated-code.md).
- The schema evolution constructs — see
  [`schema-evolution.md`](schema-evolution.md).
