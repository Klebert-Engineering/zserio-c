# 04 · C++ runtime internals

**Purpose.** Deep-read of the upstream C++ runtime — module decomposition,
hot paths, and (the main reason for this document) the `pmr/`
allocator subtree. Output: a concrete proposal for the
`zserio_allocator_t` ABI we will lock at the start of Phase 1, plus a
catalog of techniques we want to adopt and anti-patterns we explicitly
reject.

**Authoritative sources** (at pinned tag `v2.18.1`):
- `compiler/extensions/cpp/runtime/src/zserio/` — 65 public headers + 13 `.cpp`
- `compiler/extensions/cpp/runtime/src/zserio/pmr/` — 18 files, the polymorphic
  memory resource subtree
- `compiler/extensions/cpp/runtime/test/` — runtime unit tests, useful as
  oracle for behaviors that aren't in the headers

## 1. Module decomposition of the upstream runtime

Grouping the 78 files into functional clusters:

### 1.1 Wire-format core (the part we mirror)
`BitStreamReader.{h,cpp}`, `BitStreamWriter.{h,cpp}`, `BitFieldUtil.{h,cpp}`,
`BitPositionUtil.h`, `BitSizeOfCalculator.{h,cpp}`, `BitBuffer.h`,
`SizeConvertUtil.{h,cpp}`, `FloatUtil.{h,cpp}`, `BuiltInOperators.{h,cpp}`,
`Span.h`, `Types.h`, `RuntimeArch.h`, `CppRuntimeVersion.h`.

This is what `02-wire-format.md` already specifies. We replicate
behaviorally; we do NOT replicate the C++ surface (templates, exceptions).

### 1.2 Allocator system
`pmr/MemoryResource.{h,cpp}`, `pmr/NewDeleteResource.{h,cpp}`,
`pmr/PolymorphicAllocator.h`, `AllocatorHolder.h`,
`AllocatorPropagatingCopy.h`, `RebindAlloc.h`, plus the per-container
PMR aliases (`pmr/Vector.h`, `pmr/String.h`, `pmr/Map.h`, `pmr/Set.h`,
`pmr/UniquePtr.h`, `pmr/HeapOptionalHolder.h`, `pmr/AnyHolder.h`,
`pmr/BitBuffer.h`, `pmr/ArrayTraits.h`).

This is the focus of the document. Our `zserio_allocator_t` is informed
by — but materially different from — this subsystem (§3, §4).

### 1.3 Container glue (we replace)
`Vector.h`, `String.h`, `StringView.h`, `OptionalHolder.h`,
`UniquePtr.h`, `Array.h`, `ArrayTraits.h`, `AnyHolder.h`,
`HashCodeUtil.h`, `Traits.h`, `NoInit.h`, `Enums.h`.

These wrap STL containers with allocator propagation and zserio-specific
helpers. We do not replicate any of them. Generated C code holds buffers
inline as `T *data; size_t size; size_t capacity;`-style triples,
allocated through our vtable. No abstraction layer.

### 1.4 Exception classes (we replace with status codes)
`CppRuntimeException.{h,cpp}`, `ConstraintException.h`,
`PubsubException.h`, `ServiceException.h`, `SqliteException.h`.
**Six exception classes.** All replaced by `zserio_status_t` per
REQ-FS-002.

### 1.5 Reflection / introspection (we omit entirely)
`IReflectable.h`, `Reflectable.h`, `ReflectableUtil.h`, `ITypeInfo.h`,
`TypeInfo.h`, `TypeInfoUtil.{h,cpp}`. Plus the per-container `pmr/`
mirrors of these.

Forbidden by REQ-FS-008. Removing this entire cluster is one of the
biggest size and complexity savings vs. the upstream runtime.

### 1.6 Walk / debug / validation (we omit)
`Walker.h`, `WalkerConst.h`, `IWalkObserver.h`, `IWalkFilter.h`,
`IValidationObserver.h`, `DebugStringUtil.h`, `ParsingInfo.h`,
`StringConvertUtil.h`, `ZserioTreeCreator.{h,cpp}`.

Diagnostic / generic-walking machinery built on top of reflection.
Out of v1 scope. Removing these is essentially free (they are leaves;
nothing in our wire path depends on them).

### 1.7 JSON I/O (we omit)
`JsonEncoder.{h,cpp}`, `JsonDecoder.h`, `JsonParser.h`, `JsonReader.h`,
`JsonTokenizer.{h,cpp}`, `JsonWriter.h`. Confirmed out of v1 in
`01-schema-language.md` §3.

### 1.8 Service / Pubsub / SQLite (we omit)
`IService.h`, `ServiceException.h`, `IPubsub.h`, `PubsubException.h`,
`ISqliteDatabase.h`, `ISqliteDatabaseReader.h`, `SqliteConnection.h`,
`SqliteException.h`, `SqliteFinalizer.h`, `ValidationSqliteUtil.h`.
Out of v1 per `01`.

### 1.9 Packed-array machinery (we replicate, simplified)
`DeltaContext.h`. Algorithm covered in `02-wire-format.md` §5.5;
implementation port is straightforward C99 (state machine + bit-size
accumulator).

### 1.10 Misc / utilities (selective)
`FileUtil.{h,cpp}` — host-side only, file I/O. We will provide
`SerializeUtil.h`-style entry points but they are file-I/O glue, not
codec internals.
`SerializeUtil.h` — convenience wrappers around the runtime to
"serialize an object to a file" or to a memory buffer.
`DeprecatedAttribute.h` — `[[deprecated]]` macro indirection. We use
`__attribute__((deprecated))` directly in C99 builds where supported.

## 2. The numbers

| Cluster                       | Files | v1 status                                     |
|-------------------------------|------:|------------------------------------------------|
| Wire-format core              |    18 | Replicate, simplified                          |
| Allocator system (pmr/, glue) |    23 | Inform our `zserio_allocator_t`; redesign      |
| Container glue                |    12 | Drop entirely                                  |
| Exception classes             |     6 | Drop entirely (status codes)                   |
| Reflection / introspection    |    10 | Drop entirely (REQ-FS-008)                     |
| Walk / debug / validation     |     9 | Drop entirely                                  |
| JSON I/O                      |     7 | Drop entirely                                  |
| Service / Pubsub / SQLite     |    10 | Drop entirely                                  |
| Packed-array machinery        |     1 | Replicate                                      |
| Misc / utilities              |     6 | Selective                                      |

About **66 files of upstream cluster mass dropped** out of ~102 total.
What remains we either replicate behaviorally, or use as design input
without replicating the surface.

## 3. The pmr/ subtree — read-through

### 3.1 `MemoryResource` (`pmr/MemoryResource.h`, 188 lines)

Abstract base class. Three pure-virtual methods plus their public
non-virtual wrappers:

```cpp
class MemoryResource {
public:
    void* allocate  (size_t bytes, size_t alignment = alignof(max_align_t));
    void  deallocate(void* storage, size_t bytes, size_t alignment = alignof(max_align_t));
    bool  isEqual   (const MemoryResource& other) const noexcept;
private:
    virtual void* doAllocate  (size_t bytes, size_t alignment) = 0;
    virtual void  doDeallocate(void* storage, size_t bytes, size_t alignment) = 0;
    virtual bool  doIsEqual   (const MemoryResource& other) const noexcept = 0;
};
```

Notable design choices we keep:

- **Sized deallocation**: `bytes` and `alignment` are passed back to
  `deallocate`. This lets pool allocators avoid storing per-block
  metadata; they can locate the originating slot from `(ptr, size,
  alignment)` directly.
- **Explicit alignment**: every allocation carries the required
  alignment. Default is `alignof(max_align_t)` (worst case). We require
  explicit alignment too — it is cheap to thread through and essential
  for SIMD/structure-alignment use cases later.

Notable design choices we **reject**:

- **Virtual dispatch**: the base class uses C++ virtual functions.
  In C99 we use a function-pointer vtable struct. Same indirection cost
  at the call site, no C++ ABI dependency, and amenable to MISRA
  Rule 8 / Rule 12.x analyzes without special handling.
- **`isEqual`**: needed only when containers move between resources
  (so a Vector created with allocator A can be assigned to a
  Vector backed by allocator B; `isEqual` checks compatibility).
  Our model has no container abstraction and no allocator propagation;
  every owning structure is bound to one allocator at construction.
  The equality predicate is unnecessary.
- **Tracing macro** (`ZSERIO_MEMORY_RESOURCE_TRACING`): a debug-build
  affordance that wraps every alloc/dealloc with a `std::cout` trace.
  We can offer the same as a `ZSERIO_C_ALLOCATOR_TRACE` callback hook
  without baking it into the vtable.

### 3.2 `PolymorphicAllocator` (`pmr/PolymorphicAllocator.h`, 245 lines)

Two flavors:

```cpp
template <class T = uint8_t>
class PolymorphicAllocator;                  // non-propagating

template <class T = uint8_t>
class PropagatingPolymorphicAllocator;       // propagating
```

Both wrap a `MemoryResource*`. The difference is in container behavior
when copied or moved — propagating allocators travel with their owning
container; non-propagating ones do not (the destination container
reverts to its default-constructed allocator on copy assignment).

Each class also publishes the C++ allocator traits
(`propagate_on_container_*`, `select_on_container_copy_construction`,
`rebind`). These are STL allocator concepts and **do not exist in our
C99 model**. There are no STL containers; allocators do not propagate;
every API takes the allocator explicitly.

Critical detail in the constructor:

```cpp
PolymorphicAllocatorBase(MemoryResource* resource = getDefaultResource()) noexcept
    : m_resource(resource != nullptr ? resource : getDefaultResource())
```

> *non-standard extension here. When the resource is NULL,
> `getDefaultResource()` is used instead!*

This is a UX-vs-safety tradeoff. Upstream chose UX: passing NULL
silently substitutes a global default resource. **We reject this.**
A NULL allocator at our public API is `ZSERIO_ERR_INVALID_ARGUMENT` —
loud failure, no implicit globals, ASIL-friendly.

### 3.3 The default resource (`pmr/MemoryResource.cpp`, 36 lines)

```cpp
MemoryResource*& getCurrentDefaultResource() {
    // static variable is in the function to support proper early initialization
    static MemoryResource* defaultResource = getNewDeleteResource();
    return defaultResource;
}

MemoryResource* getDefaultResource() noexcept { return getCurrentDefaultResource(); }
MemoryResource* setDefaultResource(MemoryResource* r) noexcept { /* swap */ }
```

This is **global mutable state**, exactly what REQ-FS-007 forbids.
Notable risks:
- Static-initialization-order issues (the comment acknowledges them).
- Setter/getter pair is documented as not safe to use concurrently.
- Two libraries linking the same runtime fight over the default.

We do not replicate. There is no `zserio_get_default_allocator()` /
`zserio_set_default_allocator()` in our API. Callers always pass an
explicit allocator. A convenience header may expose
`zserio_allocator_malloc()` returning a stateless allocator backed by
`malloc`/`free` — but that is a constructor, not a global.

### 3.4 `NewDeleteResource` (`pmr/NewDeleteResource.h/.cpp`, 63 lines total)

The default backend, wraps `::operator new` / `::operator delete`:

```cpp
class NewDeleteResource : public MemoryResource {
private:
    void* doAllocate(size_t bytes, size_t /*alignment*/) override {
        return ::operator new(bytes);
    }
    void doDeallocate(void* storage, size_t /*bytes*/, size_t /*alignment*/) override {
        ::operator delete(storage);
    }
    bool doIsEqual(const MemoryResource& other) const noexcept override {
        return this == &other;
    }
};
```

Notably, `NewDeleteResource` **ignores** the alignment parameter. On
typical platforms this is OK because `operator new` returns
fundamental-aligned memory. For aligned types beyond fundamental
alignment, this is a latent bug — but in practice zserio doesn't
require it.

Our `malloc`-backed default does the same, with the same caveat. If
strict alignment matters, callers plug a different allocator.

### 3.5 The PMR container aliases

`pmr/Vector.h`, `pmr/String.h`, `pmr/Map.h`, `pmr/Set.h`,
`pmr/UniquePtr.h`, `pmr/HeapOptionalHolder.h`, `pmr/AnyHolder.h`,
`pmr/BitBuffer.h` — each is a one-line `using` template alias of the
non-PMR equivalent with `PropagatingPolymorphicAllocator` plugged in.

For example:

```cpp
template <typename T>
using vector = zserio::vector<T, PropagatingPolymorphicAllocator<T>>;
```

We do not replicate any of these. Generated C code emits ad-hoc
`<type>_array_t` structs with explicit `data`/`size`/`capacity` and
operates on them through generated `*_init` / `*_free` / `*_resize`
helpers that take the allocator explicitly.

### 3.6 Helper classes in main runtime

- **`AllocatorHolder<ALLOC>`** — empty-base-optimization wrapper used
  to embed an allocator in a container without paying space when the
  allocator is empty. C++ specific. We carry the allocator pointer
  explicitly in struct fields where needed.
- **`AllocatorPropagatingCopy`** — utilities for performing a deep
  copy that re-binds nested containers' allocators to a new resource.
  Not applicable to our model.
- **`RebindAlloc<ALLOC, T>`** — `std::allocator_traits` rebind helper.
  C++ allocator concept; not applicable in C.

## 4. Proposed `zserio_allocator_t` ABI (the main deliverable)

This is the proposal we lock at the start of Phase 1. Lives in
`runtime/include/zserio/allocator.h`.

```c
#ifndef ZSERIO_ALLOCATOR_H_
#define ZSERIO_ALLOCATOR_H_

#include <stddef.h>
#include "zserio/status.h"

/* Function-pointer vtable. No virtual dispatch.
 * All four function pointers are required to be non-NULL except
 * `realloc`, which MAY be NULL — see realloc semantics below. */
typedef struct zserio_allocator_vtable {
    /* Allocate `size` bytes with at least `alignment` byte alignment.
     * Returns a non-NULL pointer on success, NULL on out-of-memory. */
    void *(*alloc)(void *userdata, size_t size, size_t alignment);

    /* Optional: resize the allocation at `ptr` from `old_size` to
     * `new_size` bytes, preserving the smaller of the two and
     * preserving alignment. Returns the new (possibly relocated)
     * pointer on success, NULL on out-of-memory.
     *
     * MAY be NULL. When NULL, the runtime falls back to alloc + memcpy +
     * free. Allocators that can resize in place SHOULD provide this for
     * the buffer-growth fast path during decode. */
    void *(*realloc)(void *userdata, void *ptr,
                     size_t old_size, size_t new_size, size_t alignment);

    /* Free a previously-allocated block. `ptr` MAY be NULL (no-op).
     * `size` and `alignment` MUST match what was passed to alloc/realloc. */
    void (*free)(void *userdata, void *ptr, size_t size, size_t alignment);
} zserio_allocator_vtable_t;

typedef struct zserio_allocator {
    const zserio_allocator_vtable_t *vtable;  /* MUST NOT be NULL */
    void                            *userdata;/* opaque, passed to every callback */
} zserio_allocator_t;

/* Built-in convenience constructor: returns an allocator backed by
 * malloc/free/realloc with no userdata. Stateless, safe to copy.
 * NOT a global default — explicit construction is required. */
zserio_allocator_t zserio_allocator_libc(void);

#endif /* ZSERIO_ALLOCATOR_H_ */
```

### 4.1 Why function pointers, not virtual functions

- C99 has no virtual functions, but the choice runs deeper. Function
  pointers in a vtable are explicit, MISRA Rule 8 / 12.x compatible
  with documented deviations, and toolable by every static analyzer.
- A `static const zserio_allocator_vtable_t` declaration places the
  vtable in `.rodata`, which is what we want for safety builds — no
  mutable dispatch table.

### 4.2 Why size + alignment on `free`

- Sized deallocation is what makes pool allocators efficient (no
  per-block size header).
- Symmetry with `alloc`: every block's lifetime is described by the
  same triple `(ptr, size, alignment)` from cradle to grave.
- Cost: caller must remember `size` for the lifetime of the block.
  Generated C code already does this for arrays/strings; it's free.

### 4.3 Why optional `realloc`

- `realloc` is genuinely useful for the auto-array decode fast path
  ("grow capacity by 2× to avoid quadratic copy"). When the underlying
  allocator can extend in place, this avoids a full memcpy.
- Pool allocators can't usually realloc (slot sizes are fixed). Making
  it required would force them to fall back to alloc + copy + free
  inside their own implementation — better to let the runtime do that
  fallback once.
- Decision: optional pointer. Runtime helper `_alloc_grow()` checks
  the field and either calls realloc or does the alloc/copy/free
  dance.

### 4.4 Why `userdata` instead of `MemoryResource*`

- More general. `userdata` can be any pointer — the libc allocator
  ignores it, pool allocators point it at the pool, tracking
  allocators point it at a counter, etc.
- Avoids the "allocator owns a pointer to a thing that owns the
  memory" two-level indirection of PMR.
- Same lifetime contract: the allocator's lifetime is the caller's
  responsibility; userdata must outlive any allocation.

### 4.5 Trace hook (deferred — not in v1 ABI)

Upstream's `ZSERIO_MEMORY_RESOURCE_TRACING` macro is a useful debug
affordance but not v1-critical. We can add a fourth optional vtable
slot later (`trace(userdata, kind, ptr, size)`) without breaking the
ABI: existing allocators set the new field to NULL.

If we want it from day one, we add it now:

```c
typedef enum {
    ZSERIO_TRACE_ALLOC, ZSERIO_TRACE_FREE, ZSERIO_TRACE_REALLOC,
} zserio_trace_kind_t;

void (*trace)(void *userdata, zserio_trace_kind_t kind,
              void *ptr, size_t size);
```

Recommend deferring to Phase 1 — easier to add than to remove, and
easier to design once we have one tracking allocator written for
tests.

### 4.6 What this ABI does NOT include

- **No `is_equal`** (§3.1). Our model doesn't need it.
- **No global default** (§3.3). Every API takes the allocator explicitly.
- **No allocator propagation traits** (§3.2). C99 has no containers
  carrying allocators; explicit at every call.
- **No rebinding** (§3.6). One allocator type, used uniformly for raw
  bytes.

## 5. Bitstream hot path — techniques we adopt

`compiler/extensions/cpp/runtime/src/zserio/BitStreamReader.cpp` is the
canonical implementation. Two patterns are worth lifting:

### 5.1 Bit cache + mask table

The reader keeps a host-word-sized "bit cache" register and a count of
valid bits within it. Reading N bits:

1. If cache has ≥N bits: extract `(cache >> (cacheNumBits - N)) & MASK_TABLE[N]`,
   decrement counter.
2. If not: drain the cache, refill from the buffer, append.

`MASK_TABLE[i] = (1 << i) - 1` precomputed for `i ∈ [0, sizeof(BaseType)*8]`.
Indexed by bit count; the table is `static const`.

**For our runtime**: same algorithm, same mask table. Both are
straightforward C99 with no C++-specific dependencies.

### 5.2 32-bit vs. 64-bit base type, compile-time selectable

```c
#ifdef ZSERIO_RUNTIME_64BIT
typedef uint64_t zserio_bit_register_t;
#else
typedef uint32_t zserio_bit_register_t;
#endif
```

The bit cache and mask table are sized to `zserio_bit_register_t`.
On 64-bit hosts, the cache is 64 bits and `readBits` for N ≤ 64 is
single-cache; on 32-bit hosts it falls back to a slower path for
N > 32. Avoids software 64-bit emulation on 32-bit MCUs.

We replicate. CMake auto-detects and sets `ZSERIO_RUNTIME_64BIT` when
`sizeof(void*) == 8`; manual override available.

### 5.3 Validate-then-impl pattern

```cpp
uint32_t BitStreamReader::readBits(uint8_t numBits) {
    checkNumBits(numBits);                    // boundary validation
    return static_cast<uint32_t>(readBitsImpl(m_context, numBits));
}
```

Public entry validates the argument, then delegates to a `static
inline` impl that has no validation overhead. REQ-FS-006 enforced at
the boundary; internal calls (e.g. `readVarUInt32` → `readBits`) bypass
re-validation by calling the impl directly.

We replicate. Public functions in our runtime always validate;
internal functions are documented as relying on caller contracts.

### 5.4 Maximum array preallocation (REQ-FS-005 in upstream form)

Upstream defines `ArrayPreallocation::MAX_INITIAL_ARRAY_ALLOCATION = 128 * 1024`
to guard against malicious inputs declaring giant array lengths during
decode. The reader's constructor takes a `maxArrayPrealloc` parameter
that defaults to this constant.

This is exactly the spirit of REQ-FS-005. We adopt it more strictly:
**every** array decode entry point in our runtime takes an explicit
`max_count` parameter from the caller; there is no default. If a
generated decoder doesn't have a sane bound from the schema, the
generator emits a per-call argument so the caller can decide.

## 6. Containers — what we don't replicate

`Vector.h`, `String.h`, `OptionalHolder.h`, `UniquePtr.h`, `Array.h`,
`AnyHolder.h`, and their `pmr/` mirrors are 12 files of allocator-aware
container glue. We discard all of them.

In their place, generated C code uses **per-type concrete structs**:

```c
/* Generated for: uint8 list[]; */
typedef struct foo_uint8_array {
    uint8_t *data;
    size_t   size;
    size_t   capacity;
} foo_uint8_array_t;

zserio_status_t foo_uint8_array_init(foo_uint8_array_t *self,
                                     const zserio_allocator_t *alloc,
                                     size_t initial_capacity);
void            foo_uint8_array_free(foo_uint8_array_t *self,
                                     const zserio_allocator_t *alloc);
zserio_status_t foo_uint8_array_resize(foo_uint8_array_t *self,
                                       const zserio_allocator_t *alloc,
                                       size_t new_size);
```

Pros:
- No type erasure, no virtual calls. Generated `foo_uint8_array_*`
  inlines into the encode/decode hot path.
- Memory layout is fully described in the generated header; no STL
  ABI dependency.
- Each instantiation pays only for what it uses (a `uint8` array doesn't
  carry a comparator function pointer because it has no need of one).

Cons:
- Code-size proliferation — every element type produces its own array
  family. We mitigate by sharing implementation via `static inline`
  macros / `_Generic`-free helper templates expanded by the generator.
- Generator complexity bears the abstraction.

Net: code size is competitive with upstream's STL approach in our
benchmarks-target (REQ-PF-002), and the safety case argument is cleaner
because every type in the call graph is concrete and locally analyzable.

## 7. Anti-patterns we explicitly reject

For the safety case, it's useful to enumerate what we are NOT doing.

- **Global mutable allocator state.** `getDefaultResource` /
  `setDefaultResource` (§3.3). We have neither.
- **Implicit-NULL fallback.** `PolymorphicAllocator` substitutes a
  global default when given NULL (§3.2). We return
  `ZSERIO_ERR_INVALID_ARGUMENT`.
- **Virtual dispatch.** `MemoryResource` is a class with virtual
  functions (§3.1). We use a function-pointer vtable in `.rodata`.
- **Exceptions on allocation failure.** Upstream throws
  `std::bad_alloc` from inside `operator new`; PMR forwards. We return
  status codes; `alloc` returning NULL becomes `ZSERIO_ERR_OUT_OF_MEMORY`
  surfaced from the public API.
- **Allocator propagation traits.** Container-level concept (§3.2).
  Not applicable; our generated structs hold an allocator pointer or
  receive it per-call, never propagate it implicitly.
- **`std::allocator_traits::rebind` machinery.** `RebindAlloc.h` (§3.6).
  Not applicable.
- **Reflection emitted by the generator.** REQ-FS-008. Detailed in
  `01-schema-language.md` §1.
- **Hidden static initializers.** Both PMR's default-resource lookup
  and `NewDeleteResource`'s singleton use C++ static-local-with-init
  patterns. We have zero static initializers in the runtime; the
  default `zserio_allocator_libc()` constructor returns a value
  literal.

## 8. Open questions for Phase 1

1. **Trace hook in v1 ABI?** §4.5. Recommend deferring.
2. **`alloc` return on `size == 0`**: NULL or non-NULL distinct from
   "out of memory"? Recommend: caller MUST NOT call `alloc(0)`;
   runtime never does. Public API rejects with
   `ZSERIO_ERR_INVALID_ARGUMENT` on size 0 if the request reached
   that depth.
3. **Alignment validation**: clamp to power-of-two and ≤ some max?
   `MemoryResource` accepts any size_t. Recommend: validate
   `(alignment & (alignment - 1)) == 0` and `alignment <= alignof(max_align_t) * 4`
   at the public boundary; allocators don't need to re-validate.
4. **Allocator userdata aliasing**: can two `zserio_allocator_t`
   instances share `userdata` (e.g. a single pool used by two views)?
   Answer: yes — `userdata` lifetime is the caller's; passing the same
   pointer to two allocator structs is fine. Document.
5. **Reentrant `realloc` semantics**: if `realloc` returns NULL, is
   the original block still valid? Answer: yes (POSIX-`realloc`
   semantics — failure preserves the old block). The runtime's
   grow-helper relies on this for the no-fallback case.

## 9. Acceptance

This document is *accepted* when:

- [x] Upstream runtime's 78 files are decomposed by function (§1, §2)
      and the v1 fate of each cluster is recorded.
- [x] The pmr/ subtree is read end-to-end and the relevant design
      choices are explained (§3).
- [x] A concrete `zserio_allocator_t` ABI is proposed with rationale
      (§4).
- [x] Bitstream hot-path techniques worth adopting are catalogd
      (§5).
- [x] Anti-patterns we explicitly reject are enumerated for the safety
      case (§7).
- [ ] Phase 1 open questions in §8 are resolved during runtime API
      lockdown at the start of Phase 1 — not now.

The first five are checked. §8 belongs to Phase 1.
