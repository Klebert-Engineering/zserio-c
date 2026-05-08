# `runtime/` — libzserio-c

The C runtime library that generated code links against. Public
headers live in `include/zserio/`, implementations in `src/`.

**Status: API contract locked, implementation pending.**

The public-API surface is fixed by `docs/upstream-analysis/02-wire-format.md`
§8 and `04-cpp-runtime-internals.md` §4.

Already implemented:

- **`<zserio/allocator.h>`** — `zserio_allocator_t` vtable handle and
  `zserio_allocator_libc()` constructor.
- **`<zserio/status.h>`** — `zserio_status_t` enumeration and
  `ZSERIO_TRY` macro.
- **`<zserio/bitstream.h>`** — bit-level reader and writer (byte-walk
  algorithm; cache-and-mask-table optimization deferred to Phase 6).
  Variable-width bit fields (`bit:N` / `int:N`) are accessed via the
  bitstream's `read_bits` / `read_signed` / `write_bits` / `write_signed`
  — there is no separate `bits.h` since it would only be aliases.
- **`<zserio/primitives.h>`** — fixed-width integer (`uint8`/`uint16`/
  `uint32`/`uint64`/`int8`/`int16`/`int32`/`int64`) and `bool` helpers
  on top of bitstream.
- **`<zserio/varint.h>`** — variable-length integer encoding for
  `varuint16`/`varuint32`/`varuint64`/`varuint`/`varsize` and the
  signed counterparts. INT64_MIN special-cased for `varint`. `varsize`
  decoder validates the artificial 2³¹−1 cap.
- **`<zserio/float.h>`** — IEEE 754 `float16` / `float32` / `float64`
  pack / unpack via integer-only operations on the bit pattern
  (REQ-FS-004 boundary). float16 narrowing/widening ported from
  upstream `FloatUtil.cpp`; float32/64 via `memcpy`.
- **`<zserio/string.h>`** — UTF-8 strings. Dual API: `zserio_string_view_t`
  zero-copy borrow (requires byte-aligned source) and
  `zserio_string_t` owned (allocator-injected, handles bit-misalignment
  by byte-by-byte copy). UTF-8 well-formedness NOT validated, matching
  upstream for wire-compat.
- **`<zserio/extern.h>`** — opaque bit blob. Dual API:
  `zserio_bit_view_t` (always succeeds — natively expresses
  bit_offset 0..7) and `zserio_extern_t` owned (materialized at
  byte 0 bit 0, trailing bits zero). The view path answers the
  "parse externs in one go" question with O(1).
- **`<zserio/bytes.h>`** — opaque byte blob. Dual API parallel to
  string: `zserio_bytes_view_t` (requires byte-aligned source) and
  `zserio_bytes_t` owned. Note the unit difference vs. extern: bytes
  counts BYTES, extern counts BITS.
- **`<zserio/delta.h>`** — `DeltaContext` for `packed` arrays of
  fixed-width integer types. Width-and-signedness API: caller
  passes element bit patterns as `uint64_t`. Init pass observes
  elements to compute `maxBitNumber`; use pass writes/reads. v1
  covers fixed-width integers; varint-typed packed elements are a
  follow-up.
- **`<zserio/zserio.h>`** — umbrella include.

`offset.h` is intentionally NOT a separate header. The offset feature
is implemented entirely in generated code using `bitstream.align`,
`bitstream.position`, and `bitstream.seek`. Adding a header for
this would just re-export those calls under different names.

The public API surface is now feature-complete for the in-scope
schema language (`01-schema-language.md` §1). Outstanding work:

- Performance optimization of the bitstream (cache + mask table —
  Phase 6).
- Static-analysis CI gates for REQ-FS-002 and REQ-FS-003 (no
  setjmp / no recursion).
- Code-size CI gate for REQ-PF-002.
- `<zserio/string.h>` — UTF-8 string view, owned, read / write
- `<zserio/extern.h>` — opaque bit blob (`extern` type), with the
  borrow + materialize dual API
- `<zserio/bytes.h>` — opaque byte blob (`bytes` type)
- `<zserio/delta.h>` — `DeltaContext` for packed arrays
- `<zserio/offset.h>` — offset / indexed-offset helpers
- `<zserio/zserio.h>` — umbrella include

See `docs/architecture.md` for the design rationale and
`requirements/safety/REQ-FS-001..008.md` for the constraints any
code in this directory must satisfy.
