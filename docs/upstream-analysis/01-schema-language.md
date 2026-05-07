# 01 · Schema language — v1 scope decision per construct

**Purpose.** This document classifies every construct of the zserio
schema language into one of three buckets — **In v1**, **Deferred**, or
**Out of scope** — with rationale. Together with the safety pillars
(REQ-FS-001..008), this is the master scope contract for everything
the generator must emit and everything the runtime must support.

**Source materials**, all at the pinned tag `v2.18.1`:
- `third_party/zserio/doc/ZserioLanguageOverview.md` (the canonical reference)
- `third_party/zserio/doc/ZserioQuickReference.md` (the dense summary)
- `third_party/zserio/compiler/core/antlr/ZserioParser.g4` and `ZserioLexer.g4`
- The 36 test categories under `third_party/zserio/test/extensions/language/`

## 1. Master scope table

Legend:
- **In v1** — generator emits, runtime supports, conformance tests required.
- **Deferred** — designed not to preclude, but no v1 emission. Phase number
  in parentheses indicates earliest plausible reintroduction.
- **Out** — explicit non-goal. Adding it would require a scope change and
  a new design pass.

### Primitive types (in v1)

| Construct                                      | Status | Notes                                                       |
|------------------------------------------------|--------|-------------------------------------------------------------|
| `int8`, `int16`, `int32`, `int64`              | In v1  | Fixed-width integers map to `int8_t..int64_t`.              |
| `uint8`, `uint16`, `uint32`, `uint64`          | In v1  | Fixed-width unsigned to `uint8_t..uint64_t`.                |
| `bit:n`, `int:n` with constant n               | In v1  | n ∈ [1, 64].                                                |
| `bit<expr>`, `int<expr>` (dynamic-width)       | In v1  | Width derived at decode/encode site from context.           |
| `varint16`, `varint32`, `varint64`, `varint`   | In v1  | Variable-length signed.                                     |
| `varuint16`, `varuint32`, `varuint64`, `varuint` | In v1 | Variable-length unsigned.                                  |
| `varsize`                                      | In v1  | Size-typed varuint, used by length-prefixed containers.     |
| `bool`                                         | In v1  | One bit on the wire.                                        |
| `float16`, `float32`, `float64`                | In v1  | IEEE 754 pack/unpack via integer ops only (REQ-FS-004).     |
| `string`                                       | In v1  | UTF-8 with `varsize` length prefix.                         |
| `bytes`                                        | In v1  | Raw byte sequence with `varsize` length prefix.             |
| `extern`                                       | In v1  | Variable-length opaque bitstream blob.                      |

### User-defined type building blocks (in v1)

| Construct                  | Status | Notes                                                                       |
|----------------------------|--------|-----------------------------------------------------------------------------|
| `const`                    | In v1  | Compile-time constants emitted as `static const` in generated headers.      |
| `enum`                     | In v1  | Underlying type explicit in zserio (`enum bit:3 …`); generated as C `enum` + numeric typedef. |
| `bitmask`                  | In v1  | Generated as a typedef + `static const` named flags + helper macros.        |
| `subtype`                  | In v1  | Generated as a `typedef`. Trivial.                                          |
| `struct`                   | In v1  | The bread-and-butter compound type. Generated as a `struct` + encode/decode/free helpers. |
| `union`                    | In v1  | Untagged on the wire (smallest variant chosen by encoder); generated as a tagged C `struct` containing a `union` body and a discriminator field. |
| `choice` (parameterized switch) | In v1 | Generated as a tagged C struct keyed off the parameter.               |

### Field-level features (in v1)

| Construct           | Status | Notes                                                                                                     |
|---------------------|--------|-----------------------------------------------------------------------------------------------------------|
| `optional` keyword  | In v1  | Auto-optional (presence bit on wire). Generated as a separate `_present` bool on the host struct.         |
| `if` condition      | In v1  | Conditional optional. Same generated representation as `optional`.                                        |
| Constraints (`: expr`) | In v1 | Validated on encode and on decode. Failure returns `ZSERIO_ERR_CONSTRAINT_VIOLATED`.                    |
| Default values      | In v1  | Generated initializer helpers; no implicit construction (caller must call init explicitly).               |
| Functions           | In v1  | Schema "functions" are pure derived properties. Emitted as `static` C functions returning the computed value. |
| Arrays — fixed-size `T x[N]` | In v1 | Generated as a C array.                                                                          |
| Arrays — length-prefixed `T x[N]` (N is field) | In v1 | Length stored in the named field; runtime walks it.                            |
| Arrays — auto `T x[]` | In v1 | Length stored as a `varsize` prefix.                                                                    |
| Arrays — packed     | In v1  | Delta-encoded variants (`packed` keyword). Required by realistic zserio schemas; supported.               |
| Arrays — implicit `T x` (last-field, no length) | In v1 | Read-until-EOF. Decode requires explicit caller bound (REQ-FS-005). |
| Aligned arrays      | In v1  | `align(N)` between elements.                                                                              |
| `align(N)` between fields | In v1 | Generator emits explicit padding handling.                                                          |
| Offsets, indexed offsets | In v1 | Encode writes offsets after the rest of the struct is sized; decode validates them.                  |
| Extended members (`extend`) | In v1 | Backwards-compatible additive evolution. Critical for production schemas; in v1.                  |

### Higher-level features

| Construct                    | Status      | Notes                                                                              |
|------------------------------|-------------|------------------------------------------------------------------------------------|
| Parameterized types          | In v1       | Heavily used; generator threads parameters through encode/decode.                  |
| Templates (`<T>`, `instantiate`) | In v1   | Resolved at parse time; each instantiation gets its own generated type.            |
| Packages and imports         | In v1       | Map to header organization in generated code; one C header per zserio package.     |
| Comments / doc comments      | In v1       | Doc comments propagated to generated headers.                                      |
| Annotations — `@deprecated`  | In v1       | Surfaced as a `__attribute__((deprecated))` in supported compilers; comment otherwise. |
| Annotations — others         | Deferred (3) | Most are generator hints; revisit when concrete need surfaces.                    |
| Rules                        | Deferred (5) | Documentation-only constructs; do not affect wire format. No v1 generation.       |

### Out of v1 (decided 2026-05-07)

| Construct                                 | Status   | Reason                                                                   |
|-------------------------------------------|----------|--------------------------------------------------------------------------|
| `service` types                           | Deferred | RPC layer; orthogonal to codec. Out of automotive ECU primary need.      |
| `pubsub` types                            | Deferred | Same reasoning as services.                                              |
| `sql_table`, `sql_database`, `sql_*`      | Deferred | SQLite is not a typical automotive ECU concern; pulls in heavy dependency. |
| **JSON I/O (encode/decode to/from JSON text)** | **Deferred** | **Inspection / debug-data tool, not an ASIL-B serialization need. Reintroduces in Phase 5+.** |
| Reflection / `ITypeInfo` / `IReflectable` | Out      | Forbidden by REQ-FS-008.                                                 |
| Walk / validation tooling                 | Out      | Depends on reflection; not needed for the codec proper.                  |
| Debug-string utilities                    | Out      | Depend on reflection.                                                    |

## 2. Rationale for non-obvious decisions

**Packed arrays kept in v1.** Tempting to defer because they are a wire-format
optimization, but zserio schemas in real use lean on them heavily (delta
contexts can shave 30–60% off some payloads). Excluding them would mean
the v1 codec cannot read mainstream schemas. Worth the runtime complexity.

**Implicit arrays kept in v1.** They are the right tool for "this is the
last field; consume the rest of the buffer." Bounded cleanly by REQ-FS-005
since the caller passes a buffer of finite size.

**Templates kept in v1.** Resolved at codegen time, so the *runtime*
sees only fully-instantiated types — no template machinery in the C
output. The generator becomes more complex; the runtime does not.

**Extended members kept in v1.** Schema evolution without recompiling
existing readers is a basic production requirement. Cutting it would
make v1 a toy.

**Functions kept in v1.** They are pure expressions over struct state.
Implementing them as `static inline` C functions is essentially free at
runtime cost. Excluding them would break a noticeable fraction of test
schemas and add no safety value.

**Rules deferred (not "Out").** They are documentation, not behavior, and
have no wire-format implications. Picking them up later (Phase 5, when
the safety case asks for traceability between schema-level rules and
generated code) is cheap.

**Annotations: only `@deprecated` in v1.** It earns its keep by guiding
schema migration. Other annotations (formatting hints, tool-specific
markers) we add when concrete consumers ask.

## 3. JSON I/O — deferred (decided 2026-05-07)

JSON I/O is not in v1 scope. It is an inspection / debug-data tool, not
something that belongs on the ASIL-B serialization path. Removing it
also dissolves the conflict it would have created with REQ-FS-008
(no runtime reflection).

When (if) JSON is reintroduced in Phase 5+, the path forward is **per-type
generated encoders / decoders**, not a runtime type walker — that is the
only design compatible with REQ-FS-008. Recording it here so the future
work item starts with the right design.

## 4. v1 conformance target — mapping to upstream test categories

All 36 categories under `test/extensions/language/`, classified:

**In v1 — 26 categories (target ≥99% pass before declaring Phase 4 complete):**

| # | Category               | Maps to construct group         |
|---|------------------------|---------------------------------|
| 1 | `alignment`            | Alignment & offsets             |
| 2 | `array_types`          | Arrays (all variants)           |
| 3 | `bitmask_types`        | Bitmasks                        |
| 4 | `builtin_types`        | Primitive integers & bool       |
| 5 | `choice_types`         | Choices                         |
| 6 | `comments`             | Doc comments propagation        |
| 7 | `constants`            | `const` declarations            |
| 8 | `constraints`          | Field constraints               |
| 9 | `default_values`       | Default values                  |
| 10 | `enumeration_types`   | Enums                           |
| 11 | `explicit_parameters` | Parameterized types — explicit  |
| 12 | `expressions`         | Expression evaluation           |
| 13 | `extended_members`    | `extend` keyword                |
| 14 | `functions`           | Schema functions                |
| 15 | `identifiers`         | Identifier rules                |
| 16 | `indexed_offsets`     | Offsets — per-array-element     |
| 17 | `literals`            | Literal forms                   |
| 18 | `member_access`       | Field/member access in exprs    |
| 19 | `offsets`             | Offsets — flat                  |
| 20 | `optional_members`    | `optional` and `if`             |
| 21 | `packages`            | Packages and imports            |
| 22 | `parameterized_types` | Parameterized types — full      |
| 23 | `structure_types`     | Structures                      |
| 24 | `subtypes`            | `subtype`                       |
| 25 | `templates`           | Templates                       |
| 26 | `union_types`         | Unions                          |

**Deferred — 10 categories (excluded from v1 conformance gate):**

| # | Category                       | Reason                                              |
|---|--------------------------------|-----------------------------------------------------|
| 1 | `pubsub_types`                 | Pub/sub deferred (§1)                               |
| 2 | `service_types`                | Services deferred (§1)                              |
| 3 | `sql_constraints`              | SQLite deferred (§1)                                |
| 4 | `sql_databases`                | SQLite deferred (§1)                                |
| 5 | `sql_tables`                   | SQLite deferred (§1)                                |
| 6 | `sql_types`                    | SQLite deferred (§1)                                |
| 7 | `sql_virtual_columns`          | SQLite deferred (§1)                                |
| 8 | `sql_virtual_tables`           | SQLite deferred (§1)                                |
| 9 | `sql_without_rowid_tables`     | SQLite deferred (§1)                                |
| 10 | `rules`                       | Documentation-only construct (§1)                   |

## 5. Implications for the runtime API

These follow directly from the v1 scope above and shape the runtime
public headers we write in Phase 1:

- The runtime never needs an `ITypeInfo`-equivalent; per-type generated
  code calls runtime primitives directly.
- The runtime needs a `zserio_bitstream_t` that supports bit-level read
  and write with explicit alignment operations (REQ-FS-005 bounds via
  caller-provided buffer length).
- Variable-length integer encode/decode helpers are runtime primitives
  (`zserio_read_varuint64`, `zserio_write_varuint64`, etc.); per-width
  variants exist because their byte budgets differ.
- Float pack/unpack is in the runtime, not generated, because it is
  type-uniform.
- Compound-type lifecycle (init, free, deep-copy if needed) is **per-type
  generated code**, not runtime-driven. The runtime offers no `walk()`
  or generic free.
- Arrays are runtime-supported via thin templates: a `zserio_array_*`
  family generated per element type. No type erasure.
- Optional fields are handled by per-type code (a `_present` flag);
  no runtime "optional holder" type is needed.
- **`extend` fields**: per-type generated code mirrors upstream's
  algorithm (see §6.3 for details). The runtime exposes
  `zserio_bitstream_remaining_aligned_to_byte(bs) > 0` as the
  end-of-stream probe used by extend-field decode.
- **`extern` and `bytes`**: the runtime exposes both an O(1) borrowed
  view (`zserio_bit_view_t {const uint8_t *base; size_t bit_offset;
  size_t bit_length}` for `extern`; `zserio_bytes_view_t` for `bytes`)
  and a materializing read that goes through the allocator. See §6.4.

## 6. Resolutions (closed 2026-05-07)

### 6.1 JSON in v1 — **CUT**

Out of v1, deferred to Phase 5+. See §3.

### 6.2 Implicit arrays — **ACCEPT, with caller-supplied bound**

Confirmed in v1. Decode requires the caller to pass an upper bound
(REQ-FS-005); the runtime stops at min(buffer remaining, caller bound).
Generator emits implicit arrays as auto-sized arrays with a
`max_count` parameter on the read function.

### 6.3 `extend` — **FULL SUPPORT, port upstream's algorithm**

How upstream C++ does it (read from
`compiler/extensions/cpp/freemarker/Structure.cpp.ftl`):

1. Each extend field is **byte-aligned** (`align(8)`) on the wire,
   before its own bits begin.
2. The struct carries a single `m_numExtendedFields` counter (per
   instance) recording how many extends are present.
3. **Decode** of each extend field is a guarded read:

   ```cpp
   if (alignTo(8, in.getBitPosition()) >= in.getBufferBitSize()) {
       /* stream exhausted -> extend absent, take default */
       return default_value;
   }
   ++m_numExtendedFields;
   in.alignTo(8);
   /* ... read field normally ... */
   ```

4. Per-field `isXxxPresent()` accessor returns
   `m_numExtendedFields > <field's index in extend list>`.
5. **Encode** writes all extend fields unconditionally (the encoder
   always produces the latest version of the schema).
6. **Setter** of any extend field "promotes" the struct to
   "all extends present" by setting the counter to total count.

What we adopt for C, unchanged in spirit:

```c
typedef struct foo {
    /* original fields */
    uint32_t main_field;
    /* extend fields */
    uint32_t new_field;       /* default zero if not present */
    uint8_t  ext_count;       /* number of extends present */
} foo_t;

static inline bool foo_is_new_field_present(const foo_t *self) {
    return self->ext_count > 0;
}
```

Read function (sketch):

```c
zserio_status_t foo_read(foo_t *out, zserio_bitstream_t *bs, ...) {
    /* main fields */
    ZSERIO_TRY(zserio_read_uint32(bs, &out->main_field));
    /* extend new_field */
    if (zserio_bitstream_aligned_eof(bs)) {
        out->new_field = 0;        /* default; ext_count stays 0 */
    } else {
        ZSERIO_TRY(zserio_bitstream_align_to(bs, 8));
        ZSERIO_TRY(zserio_read_uint32(bs, &out->new_field));
        out->ext_count = 1;
    }
    return ZSERIO_OK;
}
```

**Absent semantics** ("field absent is also ok" — your call): the
field takes the schema's default (or zero if no default). The caller
can interrogate `_is_present()` if they care to distinguish the two.
This matches upstream behavior exactly, which preserves wire-compat
(REQ-FN-001) and is sufficient for ASIL-B use. No new requirement
needed; the implementation will be tested under REQ-FN-001 conformance.

**Open follow-up (not blocking)**: lossless re-encode. Upstream's
encoder always writes all extends, so decoding a v1 payload with a v2
schema and re-encoding produces a v2-style payload with zero-default
extends — strictly larger than the original, and not byte-identical.
A future "re-encode preserves original extend count" mode is possible
but adds API complexity. Parking this for Phase 1 review when we
design the encoder API.

### 6.4 `extern` (and `bytes`) — **DUAL API: borrowed view + materialize**

Wire format (from `doc/ZserioEncodingGuide.md`):
- `extern` = `varsize bit_length` followed by `bit_length` raw bits
  at the **current bit position** (NOT byte-aligned).
- `bytes`  = `varsize byte_length` followed by `byte_length` bytes
  at the **current bit position**.

Upstream `BasicBitBuffer<ALLOC>` always materializes — allocates
`ceil(bit_length / 8)` bytes via the allocator, then bit-shifts every
byte if the source is misaligned. That is acceptable behavior but it
is not "in one go" — and for ASIL builds it forces an allocator call
on every extern read.

We do better. The runtime exposes **two** read families:

**(a) Borrowed view — O(1), no copy, no allocator**

```c
typedef struct {
    const uint8_t *base;     /* points into the source bitstream buffer */
    size_t         bit_offset;   /* 0..7, offset within first byte */
    size_t         bit_length;
} zserio_bit_view_t;

typedef struct {
    const uint8_t *base;
    size_t         length;        /* bytes, since 'bytes' is byte-aligned content */
} zserio_bytes_view_t;

zserio_status_t zserio_read_extern_view(zserio_bitstream_t *bs,
                                        zserio_bit_view_t *out);
zserio_status_t zserio_read_bytes_view(zserio_bitstream_t *bs,
                                       zserio_bytes_view_t *out);
```

The view references the source buffer; the caller must keep the source
buffer alive for the view's lifetime. This is the "parse in one go"
path: O(1), no shift, no allocator, no copy.

**(b) Materializing read — copies into a caller-allocated or
allocator-allocated buffer**

```c
zserio_status_t zserio_read_extern_into(zserio_bitstream_t *bs,
                                        const zserio_allocator_t *alloc,
                                        zserio_bit_view_t *out_owned);
zserio_status_t zserio_read_bytes_into(zserio_bitstream_t *bs,
                                       const zserio_allocator_t *alloc,
                                       zserio_bytes_view_t *out_owned);
```

The materializer detects alignment and uses memcpy on the hot path:
- **Byte-aligned `bytes`**: pure `memcpy`. The fast case.
- **Bit-misaligned `extern`** with byte-multiple length: single
  bit-shift pass, no per-bit branching (barrel-shift loop body).
- **Bit-misaligned `extern`** with non-byte-multiple length: same as
  above plus a final byte mask.

Generator: by default, decode of `extern`/`bytes` fields populates
an *owned* view via the materializing API — safest and matches the
caller's allocator policy. An opt-in per-field generator annotation
(or a generator command-line flag) selects the borrowed-view variant
for fields where zero-copy is desirable and the lifetime constraint is
acceptable.

This addresses your specific question — yes, we can parse externs
"in one go": the borrowed-view path always does, and the materialize
path does too on the byte-aligned fast case (which we should encourage
schemas to use via `align(8)` in front of `extern` fields).

**No new requirement needed** for either API; both are runtime affordances
governed by REQ-FS-001 (allocator) and REQ-FS-006 (input validation).

### 6.5 Packed arrays — **FULL SUPPORT**

Confirmed: match upstream's full nested `DeltaContext` machinery for
packed compound arrays. Anything less is a wire-compat hole under
REQ-FN-001. Implementation detail belongs in `02-wire-format.md` and
the runtime `zserio_delta_context_*` family.

## 7. Acceptance criteria for the v1 schema scope

This document is *accepted* (and locks the construct list above) when:
- [x] All 36 upstream test categories enumerated and classified (§4).
- [x] Every "In v1" construct has at least one upstream test category
      mapped to it in §4.
- [x] Every open question in §6 has a recorded resolution.

**All three boxes checked as of 2026-05-07. The v1 schema scope is
locked.** Phase 1 (runtime primitives) and Phase 2 (generator extension)
can proceed against this scope without re-opening it. Future scope
changes go through a new document that supersedes this one.
