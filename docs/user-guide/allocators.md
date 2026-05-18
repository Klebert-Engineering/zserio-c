# Allocators

Every `_read` and `_free` call takes a `const zserio_allocator_t *`.
The decoder allocates variable-length data (strings, dynamic
arrays, owned blobs) through this vtable. The encoder does not
allocate.

This page covers the three supported backends and how to size them.

## Allocator API

A `zserio_allocator_t` is a three-function vtable plus an opaque
state pointer:

```c
typedef struct zserio_allocator_vtable {
    void *(*alloc)  (void *userdata, size_t size, size_t align);
    void  (*free)   (void *userdata, void *ptr, size_t size, size_t align);
    void *(*realloc)(void *userdata, void *ptr, size_t old_size,
                     size_t new_size, size_t align);
} zserio_allocator_vtable_t;

typedef struct zserio_allocator {
    const zserio_allocator_vtable_t *vtable;
    void                            *userdata;
} zserio_allocator_t;
```

The vtable is a separate const struct so multiple allocator
instances can share one function pointer set while each carries
its own state. `alloc` must return NULL on exhaustion — never
abort. The runtime maps a NULL return to
`ZSERIO_ERR_OUT_OF_MEMORY` and propagates it up the call chain.

## `zserio_allocator_libc` — reference

`runtime/src/allocator.c`. Thin wrapper around libc
`malloc` / `free` / `realloc`. The `userdata` is unused.

```c
const zserio_allocator_t alloc = zserio_allocator_libc();
```

**When to use it:** development, testing, host-side tools, any
production environment where libc is qualified for the target's
safety level.

**Pros:** zero ceremony, well-understood. **Cons:** unbounded
fragmentation over long runs, libc-dependent latency, no
high-water-mark observation.

## `zserio_allocator_arena` — bump arena

Bounded bump arena backed by a fixed pool. The caller declares
a `zserio_arena_t` over a backing buffer; `alloc` is a pointer add,
`free` is a no-op, `zserio_arena_reset(&arena)` reclaims the whole
arena in O(1) between decodes.

```c
static uint8_t pool[64 * 1024];                  /* fixed budget */

zserio_arena_t arena;
zserio_arena_init(&arena, pool, sizeof pool);
const zserio_allocator_t alloc = zserio_allocator_arena(&arena);

/* …decode one message, or several… */
my_message_t msg;
my_message_init(&msg);
zserio_status_t st = my_message_read(&msg, &reader, &alloc);

/* `my_message_free` is a no-op against the arena — the reset
 * below reclaims everything at once. */
zserio_arena_reset(&arena);
```

The arena exposes `arena.high_water` — the maximum bytes consumed
since the last reset. Read it after a worst-case payload to size
your pool.

**When to use it:** embedded / automotive / any deployment with a
hard memory budget per channel. The arena gives you:

- The **bounded-memory** property the safety case can't otherwise
  guarantee — your pool size is the absolute upper bound.
- The **performance envelope** the libc allocator masks — the
  per-commit perf gate shows the arena workload running 1.2–2.0×
  faster than upstream cpp's `std::pmr` equivalent at the same
  payload (`docs/benchmarks/RESULTS.md`).
- A **safe error path**. Exhaustion returns `NULL`, propagates as
  `ZSERIO_ERR_OUT_OF_MEMORY`, no abort.

**Cons:** you must size the pool. Use
[`tools/measure_size.py`](../../tools/measure_size.py) and the
arena's high-water field on representative inputs; size with
headroom over the observed worst case.

### Sizing in practice

Take your channel's worst-case message envelope (the maximum
schema-permitted payload across all decoded message types). Run
that envelope through `zserio_arena_init` with a generous initial
pool, decode, observe `arena.high_water`, add headroom for
toolchain version drift (≥ 25% is typical), commit that as your
production pool size.

For schemas with variable-length arrays whose bounds are
schema-declared, the worst case is `length × element_size +
array_overhead`. For unbounded arrays (e.g. `T arr[]` without a
constraint), the wire format permits up to 2³¹-1 elements; the
practical bound is your channel's max message size in bytes.

## Custom vtable

For everything in between — multi-message reuse with selective
free, fixed-pool with size classes, slab allocator, qualified
embedded RTOS allocator — implement the three-function vtable
yourself:

```c
static void *my_alloc(void *ud, size_t size, size_t align) { … }
static void  my_free (void *ud, void *p, size_t size, size_t align) { … }
static void *my_realloc(void *ud, void *p,
                        size_t old_size, size_t new_size,
                        size_t align) { … }

static const zserio_allocator_vtable_t my_vtable = {
    .alloc   = my_alloc,
    .free    = my_free,
    .realloc = my_realloc,
};

const zserio_allocator_t alloc = {
    .vtable   = &my_vtable,
    .userdata = &my_pool_state,
};
```

Contract:

- `alloc(ud, size, align)` — return `align`-aligned memory of
  `size` bytes, or `NULL` on exhaustion. `size > 0`. `align` is a
  power of two, ≥ `sizeof(void *)`.
- `free(ud, ptr, size, align)` — release memory previously returned
  by `alloc` / `realloc` with the matching `size` and `align`.
  `ptr == NULL` is a no-op.
- `realloc(ud, ptr, old_size, new_size, align)` — grow or shrink
  to `new_size`. May return a different pointer. NULL `ptr` is
  equivalent to `alloc(new_size)`. `new_size == 0` is equivalent
  to `free(ptr, old_size)` and returns NULL.

The runtime calls each function with the exact `size` / `align`
pair across `alloc` ↔ `free` ↔ `realloc`. This is what lets a
size-class slab implementation work without bookkeeping.

## Allocator boundaries

- **The encoder never allocates.** `_write` reads your struct,
  writes bytes. No allocator. If a function needs scratch space
  it allocates on the stack within the per-function stack budget.
- **The reader allocates only at variable-length boundaries.**
  Fixed-size fields, scalars, fixed-length arrays, and enums all
  write to existing struct storage. Strings, owned `bytes`/`extern`
  blobs, and variable-length arrays trigger one allocation each.
- **The reader propagates allocator failures.** Any `NULL` from
  `alloc` becomes `ZSERIO_ERR_OUT_OF_MEMORY` and the read
  short-circuits. The partially-populated struct is safe to
  `_free`.

## Sizing and safety case

For ISO 26262 use, the allocator choice shapes the memory-bound
analysis the integrator owes the safety case:

- libc — bound is delegated to libc's qualification.
- arena — bound is the pool size you set, demonstrable by static
  inspection.
- custom — bound is whatever your vtable guarantees; document the
  guarantee at the same rigor as the runtime's other safety
  contracts.

See [`integration-guide.md`](../integration-guide.md) § 1 and
[`safety-case/hazard-analysis.md`](../safety-case/hazard-analysis.md)
H-03 for the full delegation.
