# `runtime/` — libzserio-c

The C runtime library that generated code links against. Public
headers live in `include/zserio/`, implementations in `src/`.

**Status: API contract locked, implementation pending.**

The public-API surface is fixed by `docs/upstream-analysis/02-wire-format.md`
§8 and `04-cpp-runtime-internals.md` §4. The first two headers are in
place:

- **`<zserio/allocator.h>`** — `zserio_allocator_t` vtable handle and
  `zserio_allocator_libc()` constructor. Locked.
- **`<zserio/status.h>`** — `zserio_status_t` enumeration and
  `ZSERIO_TRY` macro. Locked.

To be added during Phase 1 implementation:

- `<zserio/bitstream.h>` — bit-level reader / writer
- `<zserio/primitives.h>` — fixed-width integer + bool read / write
- `<zserio/bits.h>` — variable-width bit fields
- `<zserio/varint.h>` — variable-length integer encoding
- `<zserio/float.h>` — float16 / float32 / float64 pack / unpack
- `<zserio/string.h>` — string view, owned, read / write
- `<zserio/extern.h>` — opaque bit blob (extern type)
- `<zserio/bytes.h>` — opaque byte blob (bytes type)
- `<zserio/delta.h>` — `DeltaContext` for packed arrays
- `<zserio/offset.h>` — offset / indexed-offset helpers
- `<zserio/zserio.h>` — umbrella include

See `docs/architecture.md` for the design rationale and
`requirements/safety/REQ-FS-001..008.md` for the constraints any
code in this directory must satisfy.
