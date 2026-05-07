# `runtime/` — libzserio-c

The C runtime library that generated code links against. Public headers
live in `include/zserio/`, implementations in `src/`.

**Phase 0 status: empty.** See `docs/architecture.md` for the design and
`requirements/safety/REQ-FS-001..006.md` for the constraints any code in
this directory must satisfy.

Public-API sketch (from `docs/architecture.md`):

- `<zserio/allocator.h>` — the allocator vtable
- `<zserio/status.h>` — `zserio_status_t` and the error code set
- `<zserio/bitstream.h>` — bit-level reader/writer
- `<zserio/varint.h>` — varuint / varint encode and decode
- `<zserio/zserio.h>` — umbrella include
