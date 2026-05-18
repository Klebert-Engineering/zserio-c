# `runtime/` — libzserio-c

The C99 runtime library that generated code links against. Public
headers live in `include/zserio/`, implementations in `src/`.

The top-level [README](../README.md) has the overview and links.
This directory-level README is a quick index of the headers.

## Public headers

| Header | Purpose |
|---|---|
| `<zserio/status.h>` | `zserio_status_t` enum and `ZSERIO_TRY` macro. |
| `<zserio/allocator.h>` | `zserio_allocator_t` vtable handle, `zserio_allocator_libc()`, `zserio_allocator_arena()`. |
| `<zserio/arena.h>` | Bounded bump arena (`zserio_arena_t`). |
| `<zserio/bitstream.h>` | Bit-level reader / writer. |
| `<zserio/primitives.h>` | Fixed-width integer helpers on top of bitstream. |
| `<zserio/varint.h>` | Variable-length integer encoding. |
| `<zserio/float.h>` | IEEE 754 float16 / 32 / 64 pack/unpack. |
| `<zserio/string.h>` | UTF-8 strings (view + owned). |
| `<zserio/extern.h>` | Opaque bit blob (view + owned). |
| `<zserio/bytes.h>` | Opaque byte blob (view + owned). |
| `<zserio/delta.h>` | `DeltaContext` for `packed` arrays. |
| `<zserio/types.h>` | Shared types referenced by generated code. |
| `<zserio/zserio.h>` | Umbrella include. |

## Design constraints

The runtime is gated by:

- **REQ-FS-001 .. REQ-FS-008** — safety requirements; see
  `requirements/safety/`.
- **MISRA C:2012 strict** — deviations enumerated in
  [`docs/misra-deviations/`](../docs/misra-deviations/).
- **No implicit heap** — CI greps for `malloc` / `free` / `realloc`
  / `calloc` in `src/*.c` and blocks the build.
- **Stack budget** — each function ≤ 256 B; worst transitive chain
  ≤ 1 024 B; enforced by `tools/measure_stack_usage.py`.

See [`docs/architecture.md`](../docs/architecture.md) for the
design rationale.

## Building stand-alone

The runtime is normally built alongside the rest of the tree via
the top-level CMake. To build just the runtime:

```sh
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target zserio_c_runtime
```

Or compile against your own build system — see
[`docs/user-guide/build-integration.md`](../docs/user-guide/build-integration.md).
