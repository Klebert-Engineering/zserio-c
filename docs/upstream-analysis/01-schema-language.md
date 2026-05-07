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

## 3. JSON I/O — keep, but resolve the reflection tension

You chose to keep JSON I/O in v1 scope. That decision conflicts with
REQ-FS-008 (no runtime reflection). Upstream's JSON encode/decode walks
generated types via `IReflectable`, which is exactly what we forbid.

**Resolution proposed for v1**: emit JSON encode/decode as **per-type
generated C functions**, not as a runtime walker.

For each generated zserio type `Foo`, the generator additionally emits:

```c
zserio_status_t foo_to_json(const foo_t *self, zserio_json_writer_t *w);
zserio_status_t foo_from_json(foo_t *out, zserio_json_reader_t *r,
                              const zserio_allocator_t *alloc);
```

Implementation walks the schema fields directly in the emitted code —
no `ITypeInfo` table, no string compares against runtime member names.
Field name strings are local string literals inside each per-type
function and compile out when JSON is not used.

**Build-target separation.** JSON support is opt-in at the generator
level (`--with-json`) and at the CMake target level
(`ZSERIO_C_BUILD_JSON=OFF` by default). Safety builds compile without
it; the generated `.c` files conditional-compile the JSON helpers
behind `#if ZSERIO_HAS_JSON`. This keeps the safety case argument
simple ("the safety build does not contain JSON code") without
requiring two separate generator runs.

**Cost.** Generator templates roughly double in scope (one extra
per-type function family). Wire-format encode/decode is unaffected.

**Open question for you:** is this resolution acceptable, or do you
want JSON pulled out of v1 scope after all? It is the highest-effort
piece of v1 by a clear margin; cutting it is a defensible pragmatic
move and we can re-add it in Phase 5+ once the binary codec is mature.
See `## 6. Open questions` below.

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
- JSON emission lives in a separate, optional family of runtime
  functions (`zserio_json_writer_*`, `zserio_json_reader_*`) callable
  only by the per-type JSON helpers the generator emits.
- Optional fields are handled by per-type code (a `_present` flag);
  no runtime "optional holder" type is needed.

## 6. Open questions

1. **JSON in v1 — keep or cut?** §3 above. Reaffirm or revise.
2. **`extern` and `bytes` interaction with the allocator.** Both are
   variable-length blob-like types. The generated owning struct must
   hold a pointer + length, and the decode path must allocate the
   buffer through the allocator vtable. Encode path takes a borrow.
   Is this the right ownership model, or do we want a "borrowed view"
   variant for zero-copy decode of `bytes` from a long-lived buffer?
3. **Implicit arrays — reject or accept?** Their decode requires an
   explicit per-call buffer bound from the caller. Accepting them is
   the path forward; just confirming this is the call.
4. **Packed arrays — full delta-context support?** Upstream supports
   nested `DeltaContext` machinery for packed compound arrays. v1 either
   matches that surface or supports only flat-element packing. Recommend
   matching upstream — anything less is a wire-compat hole (REQ-FN-001).
5. **`extend` field migration semantics.** When decoding an old payload
   with a new schema, missing `extend` fields take their default-zero
   value. Confirm that's the behavior we want for v1, or whether we
   prefer surfacing a status code that the field was absent.

## 7. Acceptance criteria for the v1 schema scope

This document is *accepted* (and locks the construct list above) when:
- [x] All 36 upstream test categories enumerated and classified (§4).
- [x] Every "In v1" construct has at least one upstream test category
      mapped to it in §4.
- [ ] Every open question in §6 has a recorded resolution (either here
      or in a follow-up document).

Until those three boxes are checked, treat this document as a
**proposed** scope set. Phase 1 work (runtime primitives) does not depend
on closure of the open questions, so it can begin in parallel.
