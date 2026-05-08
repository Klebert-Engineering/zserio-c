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

To be added next:

- `<zserio/float.h>` — `float16` / `float32` / `float64` pack / unpack
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
