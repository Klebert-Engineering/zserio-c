# Architecture

This document describes the architecture and design principles of `zserio-c`.
It is the load-bearing reference for what the code should look like; if the
code disagrees, the code is wrong.

## Goals

1. **Wire compatibility** with upstream `ndsev/zserio` at the pinned version.
2. **Functional safety by design**: code that can be certified for
   ISO 26262 ASIL-B (and architecturally not ruled out for ASIL-D),
   without retrofit.
3. **Pure C99** for runtime and generated code, so language bindings
   to C++, Python, Rust, Go, etc. are downstream concerns and not
   built-in dependencies.
4. **Performance**: read and write throughput at least matching the
   upstream C++ runtime on equivalent hardware.

## The four design pillars

### 1. No implicit heap

Neither the runtime nor generated code calls `malloc`, `calloc`, `realloc`,
or `free` directly. All allocation flows through a caller-supplied
allocator vtable. The vtable is a separate struct so multiple allocator
instances can share one static-const set of function pointers while
each carries its own state in `userdata`:

```c
typedef struct zserio_allocator_vtable {
    void *(*alloc)  (void *userdata, size_t size, size_t alignment);
    void *(*realloc)(void *userdata, void *ptr,
                     size_t old_size, size_t new_size, size_t alignment);
    void  (*free)   (void *userdata, void *ptr, size_t size, size_t alignment);
} zserio_allocator_vtable_t;

typedef struct zserio_allocator {
    const zserio_allocator_vtable_t *vtable;
    void                            *userdata;
} zserio_allocator_t;
```

Two reference backends ship in the runtime:

- `zserio_allocator_libc()` wraps `malloc` / `realloc` / `free`. Right
  for development and for production where libc is qualified.
- `zserio_allocator_arena(&arena)` wraps a caller-owned
  `zserio_arena_t` — a monotonic upward bump over a fixed-size byte
  buffer. `alloc` is a pointer add, `free` is a no-op, and
  `zserio_arena_reset()` reclaims the whole arena in O(1) between
  decodes. Right for embedded automotive deployments where libc
  malloc isn't viable.

Safety builds may also plug a fixed-size pool or pmr-style allocator
of their own — the vtable is three function pointers and is documented
in `runtime/include/zserio/allocator.h`. This is what unblocks ASIL
use cases: there is no hidden state in the allocator, no globals, no
thread-local heap.

### 2. No exceptions, no `setjmp`

Errors propagate as explicit status codes:

```c
typedef enum zserio_status {
    ZSERIO_OK = 0,
    ZSERIO_ERR_BUFFER_OVERFLOW,
    ZSERIO_ERR_BUFFER_UNDERFLOW,
    ZSERIO_ERR_CONSTRAINT_VIOLATED,
    ZSERIO_ERR_OUT_OF_MEMORY,
    /* … */
} zserio_status_t;
```

Every fallible function returns `zserio_status_t`. Output values are written
through pointer parameters. Callers are expected to check status and may not
read output values when the call did not return `ZSERIO_OK`.

### 3. No recursion

Schema languages naturally describe trees, but recursive code is hostile
to bounded-stack analysis. The runtime and generated code use explicit,
heap-allocated worklists (drawn from the caller's allocator) instead of
function recursion. Maximum nesting depth is part of the per-schema
generated metadata so callers can size their workspace.

### 4. No floating-point in control flow

Floating-point appears only where the schema demands it (i.e. on the
encode/decode path of `float16`/`float32`/`float64` fields). It never
participates in branching, loop bounds, or sizing. This is what makes
the code amenable to MISRA C:2012 Dir 1.1 / Rule 1.3 analysis on
mixed targets.

## Module boundaries

```
                    ┌─────────────────────────────┐
                    │  Java zserio (upstream)     │  third_party/zserio
                    │  parser + sema + plugin SPI │
                    └──────────────┬──────────────┘
                                   │ ZserioExtension SPI
                    ┌──────────────▼──────────────┐
                    │  generator/                 │
                    │  zserio-c-extension (Java)  │
                    │  AST → FreeMarker → C       │
                    └──────────────┬──────────────┘
                                   │ emits .c / .h
                    ┌──────────────▼──────────────┐
                    │  generated code             │
                    │  pure C99, links runtime    │
                    └──────────────┬──────────────┘
                                   │ #include <zserio/...>
                    ┌──────────────▼──────────────┐
                    │  runtime/  libzserio-c      │
                    │  bitstream, varint, alloc   │
                    └─────────────────────────────┘
```

In Phase 7, the Java layer is replaced by a C-native parser. The runtime
and generated-code surface do not change.

## Threading model

The runtime is **reentrant but not thread-safe**. Each `zserio_bitstream_t`
and each generated-type instance must be owned by exactly one thread at a
time. There is no global state. Callers wishing to share data across
threads do so with their own synchronization, not the runtime's. This
matches AUTOSAR Classic expectations and avoids any need for atomics
in the certified subset.

## Phase plan

| Phase | Scope                                                                  | Status                              |
|-------|------------------------------------------------------------------------|-------------------------------------|
| 0     | Bootstrap, upstream analysis.                                          | done                                |
| 1     | Runtime MVP: bitstream, varints, basic primitives. 100% line+branch.   | done                                |
| 2     | Java generator extension; hello-world end-to-end.                      | done                                |
| 3     | Full schema-language coverage (compounds, choices, parameterized…).    | done                                |
| 4     | Conformance against the upstream zserio test corpus.                   | done — xvalidation gate (182 × 5)   |
| 5     | Safety case maturation — MISRA-clean pass, ISO 26262 work products.    | substantively complete (all `draft`) |
| 6     | Performance benchmarks vs. C++ runtime; tune.                          | active — measurement framework live (advisory CI gate) |
| 7     | C-native parser replaces the Java front-end.                           | open                                |

The current phase is recorded at the repository root in `.phase` and
read by `tools/check_traceability.py`. Every accepted requirement
whose `phase_target <= .phase` must have a `Verifies:` marker in
some test or tool; the traceability gate fails the build otherwise.

Each phase has acceptance criteria recorded against requirements in
`requirements/`. A phase ends when all its acceptance criteria are
verified by tests in CI.
