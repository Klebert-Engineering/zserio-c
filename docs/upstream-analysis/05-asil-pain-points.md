# 05 · ASIL pain points in upstream — concrete enumeration

**Purpose.** This document is the "why a port is justified" argument
in concrete, citable form. For every category of ASIL-blocking
behavior in the upstream C++ runtime, it lists specific files / lines
and explains what we do differently. It is also load-bearing for the
safety case: when a reviewer asks "what specifically did you not
inherit from upstream and why", this is the answer.

**Authoritative sources** (at pinned tag `v2.18.1`):
- `compiler/extensions/cpp/runtime/src/zserio/` — the upstream C++ runtime
- `compiler/extensions/cpp/runtime/ClangTidySuppressions.txt` — upstream's
  own list of findings they couldn't or wouldn't fix
- `compiler/extensions/cpp/runtime/ClangTidyConfig.txt` — what they
  enabled

**Scope discipline**: this catalog covers only **v1-relevant** upstream
files. Sites in `Json*.h`, `Sqlite*.h`, `IService.h`, `IPubsub.h`,
`Walker.h`, `IValidationObserver.h`, `IReflectable.h`, `ITypeInfo.h`,
`Reflectable.h`, `DebugStringUtil.h`, `ZserioTreeCreator.h` are
**excluded from the v1 conformance gate** because we drop those whole
clusters (`04-cpp-runtime-internals.md` §1). Where they appear below
it is for contrast, not because we will inherit them.

## 1. Headline numbers

| Category                            | Upstream hits | Our v1 hits |
|-------------------------------------|--------------:|------------:|
| `throw` statements (v1-relevant only) |          45 |           0 |
| Exception classes (incl. nested)     |             6 |           0 |
| `dynamic_cast` / `typeid` uses       |             0 |           0 |
| Non-const global variables           |             1 |           0 |
| C++ placement-new sites              |          ≥30 |           0 |
| `reinterpret_cast` (in v1-relevant)  |           ≥6 |           0 |
| STL containers in public API         |          ≥10 |           0 |
| C++ exceptions inherited from `std::exception` | yes |          n/a |
| Documented clang-tidy suppressions   |          54+ |           0 (target) |

Upstream gets one thing materially right: **no RTTI** (zero `typeid`,
zero `dynamic_cast`). They emulate `std::any` and `std::optional` with
type-id pointers and aligned storage. The remaining categories all
have ASIL implications we discuss below.

## 2. Exceptions — the headline blocker

### 2.1 Exception classes

Upstream defines six exception types relevant to v1, all rooted in
`std::exception`:

| Class                         | File                                       |
|-------------------------------|--------------------------------------------|
| `CppRuntimeException`         | `CppRuntimeException.h:21`                 |
| `ConstraintException`         | `ConstraintException.h`                    |
| `BitStreamWriter::InsufficientCapacityException` | `BitStreamWriter.h:24` (nested) |
| `PubsubException`             | `PubsubException.h` *(out of v1)*          |
| `ServiceException`            | `ServiceException.h` *(out of v1)*         |
| `SqliteException`             | `SqliteException.h` *(out of v1)*          |

`CppRuntimeException` is the base for all of them. Notably, upstream
**did** make this exception class allocation-free during construction:
the message is held in a fixed-size `std::array<char, 512>` buffer
(`CppRuntimeException.h:75`). This is good engineering. It is
nevertheless still a `std::exception`-derived class, with a virtual
`what()` and a `noexcept`-marked destructor — and **throwing it goes
through the C++ ABI's `__cxa_throw`, which on most platforms allocates
exception state on a per-thread chained heap.** This last point is
implementation-defined and the fundamental ASIL issue: even if the
exception object itself doesn't allocate, the runtime machinery does.

### 2.2 The 45 throw statements in v1-relevant code

Distribution (counted from `grep "^\s*throw " --include=*.{h,cpp}`,
after excluding the deferred clusters listed in the scope note):

| File                              | Throws | Trigger                                                              |
|-----------------------------------|-------:|----------------------------------------------------------------------|
| `BitStreamReader.cpp`             |      6 | bad numBits, EOF, oversized buffer, bad bit size, varsize OOR, bad position |
| `BitStreamWriter.cpp`             |      6 | bad bit size, oversized writes (4 sites), capacity exhausted          |
| `BitFieldUtil.cpp`                |      1 | invalid bit-field length                                              |
| `SizeConvertUtil.cpp`             |      2 | size_t→uint32 OOR (2 directions)                                      |
| `Enums.h`                         |      1 | rawValue not in enum                                                  |
| `AnyHolder.h`                     |      9 | unexpected clone/move (non-clonable held type), bad type query        |
| `OptionalHolder.h`                |      4 | dereference of empty optional                                         |
| `ParsingInfo.h`                   |      2 | invalid bit position                                                  |
| `BitBuffer.h`                     |      2 | construction with mismatched buffer size / bit size                   |
| `Array.h`                         |      4 | bad array index, too-large array, bad reset, bad packed array         |
| `BitStreamReader.h` (inline)      |      4 | wraps `throwEof` / `throwNumBitsIsNotValid` helpers                   |
| `BitStreamWriter.h`               |      2 | wraps `throwInsufficientCapacityException`                            |
| `CppRuntimeException.h` chain `<<`|      2 | (no actual throws, just message-build helpers)                        |

Approximately half of these are **input validation** at decoder
boundaries (bad input → error), which is exactly the surface we
replace with `zserio_status_t` returns under REQ-FS-006. The other half
are **invariant violations** in the upstream containers (`AnyHolder`,
`OptionalHolder`, `Array`) — those don't apply to us because we don't
have those containers.

Concrete flavor of upstream throw sites:

```cpp
// BitStreamReader.cpp:228 — EOF on read
throw CppRuntimeException("BitStreamReader: Reached eof(), reading from stream failed!");

// BitStreamReader.cpp:810 — varsize value out of range
if (result > VARSIZE_MAX_VALUE) {
    throw CppRuntimeException("BitStreamReader: Read value '")
            << result << "' is out of range for varsize type!";
}

// BitFieldUtil.cpp:11 — invalid bit field width
throw CppRuntimeException("Asking for bound of bitfield with invalid length ") << length << "!";

// Enums.h:87 — invalid enum value (raised by generated code)
throw CppRuntimeException("Enum item '") << itemName << "' doesn't exist in enum '" << /*…*/ << "'!";
```

### 2.3 Why exceptions are an ASIL-B/D blocker

Concrete reasons a safety reviewer cares:

1. **Implementation-defined memory behavior.** Itanium ABI's
   `__cxa_throw` calls `__cxa_allocate_exception` which, on glibc and
   most ABIs, calls `malloc` (or a per-thread chained pool that itself
   started with `malloc`). Memory behavior on the throw path is not
   bounded.
2. **Stack unwinding is non-trivial control flow.** WCET analysis
   tools either reject programs that throw or require the analyst to
   exclude throw-edges manually — at which point they are excluded
   from the safety case.
3. **MISRA C++:2008 Rule 15-3-2 / AUTOSAR C++14 A15-1-2** prohibit
   throwing from destructors, and in practice projects targeting
   AUTOSAR Classic disable exceptions globally with `-fno-exceptions`,
   at which point any `throw` site is a compile-time refusal.
4. **Two libraries in the same binary, one with exceptions and one
   without, are not compatible.** Linking zserio's runtime into an
   exception-disabled binary requires either rebuilding the runtime
   with `-fno-exceptions` (and then every `throw` becomes
   `std::abort`) or wrapping every public call in a `try/catch` glue
   layer. Both undermine the certification argument.

### 2.4 What we do instead

Already documented in `02-wire-format.md` and locked by REQ-FS-002:
**`zserio_status_t` return codes**. Output values written through
pointer parameters; output is undefined when status ≠ `ZSERIO_OK`.
Every fallible function in our runtime returns a status. The 45
throw sites map to specific status codes; their messages move into
documentation rather than runtime strings.

## 3. RTTI — the surprising win

Upstream uses **zero** `dynamic_cast`. Upstream uses **zero** `typeid`.

This is genuine engineering discipline; many comparable projects at
similar scale would not pass this test. Credit where due.

How they avoided it:

- **`AnyHolder` uses a per-type id-pointer trick** instead of `typeid`.
  Each templated `AnyHolder::is<T>()` and `get<T>()` uses
  `TypeIdHolder::get<T>()` which returns a `static int currentTypeId;`'s
  address — a unique pointer per template instantiation
  (`AnyHolder.h` line 30+). This is ABI-stable across translation
  units sharing a definition and avoids RTTI.
- **`OptionalHolder` is type-parameterized**: there is no runtime
  type-erased optional; the held type is a template parameter.
  `dynamic_cast` would be meaningless.
- **`IReflectable` and `ITypeInfo` use virtual dispatch** to walk
  generated types, but the type-info structures are themselves
  generated `static constexpr` data — a manual reflection scheme that
  doesn't need the C++ RTTI mechanism.

So when we forbid reflection by REQ-FS-008, we are also forbidding the
`IReflectable` / `ITypeInfo` data tables and their virtual-walker
infrastructure — but not anything as basic as `typeid`. Important
distinction in the safety-case wording.

## 4. Static and global state

### 4.1 The acknowledged non-const global

`pmr/MemoryResource.cpp:17` is upstream's only non-const global, and
they document it explicitly in `ClangTidySuppressions.txt:23`:

```
# This is necessary for implementation of low level C++ allocators to mimic standard C++17
# 'std::pmr::get_default_resource' abstraction.
cppcoreguidelines-avoid-non-const-global-variables:src/zserio/pmr/MemoryResource.cpp:17
```

The variable is the function-static default-resource pointer — covered
in detail in `04-cpp-runtime-internals.md` §3.3. Static-init-order
safety is provided by the function-static idiom; thread safety is
**not provided** and is documented as a contract (caller must not
race `setDefaultResource` with anything).

We have nothing equivalent. There is no `zserio_get_default_allocator()`
in our public API. Every allocator passes through the API explicitly.

### 4.2 Function-static singleton in `getNewDeleteResource`

`pmr/NewDeleteResource.cpp:8`:

```cpp
MemoryResource* getNewDeleteResource() noexcept {
    static detail::NewDeleteResource newDeleteResource;
    return &newDeleteResource;
}
```

The same static-local pattern; safer than a namespace-level static
because of the ordered initialization guarantee, but still global
state. We don't have an equivalent — `zserio_allocator_libc()` returns
a `zserio_allocator_t` value (small struct on the stack), not a
pointer to a singleton.

### 4.3 `static const` data tables — these are FINE

For completeness: upstream has many `static const std::array<...>`
data tables, particularly in `BitSizeOfCalculator.cpp` (10 tables of
varint maximum values) and `BitStreamReader.cpp` (the 65-entry mask
table). These are read-only, link-time-resolved, have no constructor
side effects, and pose no static-init-order or thread-safety issue.

We replicate these patterns in C99 with `static const` arrays in
`.rodata`, which is what every compiler does. The MISRA C:2012 Rule
8.x family is satisfied.

## 5. Heap allocation surface

### 5.1 STL containers in the public API

`AllocatorPropagatingCopy.h:7` `#include <vector>`. `BitBuffer.h:7`
`#include <vector>`. `Vector.h` is itself a thin alias over
`std::vector` with allocator propagation. `String.h` over
`std::basic_string`. `Map.h`, `Set.h`, `UniquePtr.h` similarly.

Every generated zserio struct that contains an array, string, optional
heap value, or extern `BitBuffer` ends up dragging at least one STL
container template into its translation unit. The result:

- Allocation timing is implementation-defined per the C++ standard's
  allocator concept and the host STL.
- Templates expand into many lines of object code per type, contrary
  to REQ-PF-002 (size-tracked).
- Allocator-aware constructors must be threaded through every ctor
  call site in the generated code; STL allocator-traits machinery is
  notoriously surprising.

We replicate none of this. Generated zserio-c types use ad-hoc
`<type>_array_t` structs (`04-cpp-runtime-internals.md` §6).

### 5.2 `OptionalHolder` and `AnyHolder` use placement-new + aligned storage

`OptionalHolder.h:677` `using AlignedStorage = typename std::aligned_storage<...>::type;`
plus 8+ placement-new sites in the same file (`new (m_storage.getStorage()) T(...)`).
`AnyHolder.h:295` `union UntypedHolder` plus 5+ `reinterpret_cast` sites
(see ClangTidySuppressions.txt for the full list).

In ASIL terms:
- `std::aligned_storage` is deprecated in C++23 and was always
  controversial (clang-tidy `cppcoreguidelines-pro-type-member-init`
  reports false positives upstream needs to suppress —
  `OptionalHolder.h:693`).
- Placement-new is fine in itself, but combined with a manually
  managed lifetime in a templated wrapper, it is a non-trivial
  correctness argument.
- `reinterpret_cast` is a MISRA Rule 11.3 / AUTOSAR M5-2-7 violation
  every time. Upstream documents them all individually.

Our generated optionals carry a parallel `bool _present` and the
field's storage in the parent struct directly. No aligned-storage,
no placement-new, no reinterpret-cast.

### 5.3 Allocation on the throw path

`CppRuntimeException`'s class storage is fixed-size, but the
`StringConvertUtil` integer-to-string helpers it uses for `<<`-style
message building hit a stack buffer first; only when the message
exceeds 512 bytes does anything get truncated. We document this as
a thing upstream got right. We get the same behavior trivially in C
because we don't construct messages on the error path at all — the
status code carries all the information the caller needs.

## 6. The clang-tidy suppression file — upstream's own confession

`compiler/extensions/cpp/runtime/ClangTidySuppressions.txt` is **the
single most useful file** for explaining why a port is needed. It is a
~80-line list of clang-tidy findings upstream chose not to fix, each
with a comment justifying why. By category:

- **`bugprone-signed-char-misuse`** (6 sites in `DeltaContext.h`) —
  intentional mass-storage of any element type as `uint64_t` for delta
  compression. We replicate the design but do it in C without the
  clang-tidy noise.
- **`cppcoreguidelines-avoid-c-arrays`** (7 sites in `Span.h`/`StringView.h`) —
  emulating C++17 abstractions in C++11. Not our problem; we are C99.
- **`cppcoreguidelines-avoid-non-const-global-variables`** (1 site, §4.1)
  — the `pmr` default resource. We do not have one.
- **`cppcoreguidelines-macro-usage`** (1 site, version constant) —
  necessary for compile-time check. We use the same pattern.
- **`cppcoreguidelines-pro-type-union-access`** (whole file
  `AnyHolder.h`) — `std::any` emulation requires union access. We have
  no any-holder.
- **`cppcoreguidelines-pro-bounds-array-to-pointer-decay`** (8+ sites
  across `AnyHolder.h`, `CppRuntimeException.h`, `ZserioTreeCreator.h`,
  `Span.h`, `StringView.h`) — C-array-to-pointer decay in templates. C
  doesn't have this issue at the language level (no decay surprise
  outside generic helpers).
- **`cppcoreguidelines-pro-bounds-constant-array-index`** (~22 sites
  across `BitStreamReader.cpp`, `BitStreamWriter.cpp`, `Enums.h`,
  `JsonEncoder.cpp`, `StringConvertUtil.h`, `TypeInfo.h`) — direct
  array indexing instead of `.at()` because bounds are checked
  manually for performance. We do the same; MISRA C:2012 has no
  equivalent rule (Rule 18 family is about pointer arithmetic, not
  array indexing).
- **`cppcoreguidelines-pro-bounds-pointer-arithmetic`** (1 site,
  deprecated `BitBuffer` ctor). We don't have it.
- **`cppcoreguidelines-pro-type-member-init`** (2 sites, false
  positives) — minor.
- **`cppcoreguidelines-pro-type-reinterpret-cast`** (`AnyHolder.h:832`)
  — already covered.

The pattern: **most suppressions exist because the upstream runtime
emulates C++17 facilities** (`std::any`, `std::optional`,
`std::pmr::polymorphic_allocator`, `std::span`, `std::string_view`)
on a C++11 baseline. None of those facilities are needed in our
domain. By dropping the emulation, we drop most of the suppressions.

What remains in our world: the "direct array indexing for performance"
pattern (their `cppcoreguidelines-pro-bounds-constant-array-index`),
which we will face the equivalent of in MISRA C:2012 review and
likely deviate identically. Our deviations are documented under
`docs/misra-deviations/` (created when first needed).

## 7. C++-specific features that block ASIL

For completeness, even where upstream uses these defensively:

| Feature                              | Upstream sites               | Our v1 |
|--------------------------------------|------------------------------|--------|
| `throw` / `try` / `catch`            | 45 (§2)                      | 0      |
| `dynamic_cast`                       | 0                            | 0      |
| `typeid`                             | 0                            | 0      |
| `reinterpret_cast`                   | ≥6 in v1-relevant            | 0      |
| Placement new (`new (ptr) T(…)`)     | ≥30                          | 0      |
| `std::aligned_storage`               | `OptionalHolder.h`           | 0      |
| `std::exception` derivation          | 6 classes                    | 0      |
| `std::vector`/`string`/`map`/`set`   | pervasive                    | 0      |
| `std::shared_ptr` / `std::unique_ptr`| `UniquePtr.h` alias          | 0      |
| `std::function`                      | 0                            | 0      |
| Templates with type erasure          | `AnyHolder`, `IReflectable`  | 0      |
| Multiple inheritance                 | 0                            | 0      |
| Virtual functions                    | `MemoryResource`, `IService`, `IPubsub`, exception classes, observers | 0 |
| `noexcept` annotations               | extensive                    | n/a    |

The "Our v1: 0" column is an aspirational state, codified by REQ-FS-001
(no implicit heap), REQ-FS-002 (no non-local control flow), REQ-FS-003
(no recursion), REQ-FS-004 (no FP in control flow), REQ-FS-006
(boundary validation), REQ-FS-007 (no global state), REQ-FS-008
(no reflection). That is the safety-by-design argument restated with
upstream's actual code as the contrast.

## 8. What upstream got materially right

Worth calling out so the safety case is honest:

1. **Zero RTTI** (§3). Real engineering discipline.
2. **Fixed-buffer exception messages** (`CppRuntimeException.h:75`).
   No heap allocation during error construction. We adopt the spirit
   (no allocation on error paths) without inheriting the class.
3. **Sized deallocation** in PMR (§3.1 of `04`). Transposed to our
   `zserio_allocator_t.free(ptr, size, alignment)`.
4. **Mask tables for bit reads** (`BitStreamReader.cpp:35` onward,
   `02-wire-format.md` §5). Adopted directly.
5. **Compile-time selection of 32-bit vs. 64-bit base register**
   (`RuntimeArch.h`, `BitStreamReader.cpp:24`). Adopted directly.
6. **Maximum-array-preallocation guard** (`BitStreamReader.h:35`,
   default 128 KB). Adopted in stricter form (per-call max bound,
   no default).
7. **No global allocator coupling for typical use** — even with the
   default-resource singleton, the code paths that the allocator
   touches are explicitly threaded with template parameters; there is
   no hidden `malloc` call buried in the codec hot path.
8. **Public API validates inputs explicitly.** `BitStreamReader`'s
   `checkNumBits` is called before every bit read; `ReaderContext`
   validates the buffer/bit-size pair on construction
   (`BitStreamReader.cpp:386`). REQ-FS-006 in upstream's form.

These are what makes upstream's runtime a useful reference even where
we replace it. The port is not an indictment of the engineering — it
is a recognition that C++11 is the wrong language for the safety case
zserio-c is targeting.

## 9. Concrete summary for the safety case

When the safety case asks "what specifically did you not inherit from
upstream zserio's C++ runtime, and on what grounds":

1. **All exceptions and the exception class hierarchy** — replaced
   with `zserio_status_t` returns. Justified by REQ-FS-002 and the
   ABI/WCET concerns in §2.3.
2. **All STL containers and their `pmr/` aliases** — replaced with
   per-type generated `<type>_array_t` / `<type>_string_t` / per-type
   optional encoding. Justified by REQ-FS-001 and code-size /
   certification-surface concerns in §5.
3. **The `IReflectable` / `ITypeInfo` data tables and walker
   infrastructure** — not emitted by our generator. Justified by
   REQ-FS-008.
4. **The `pmr::get_default_resource` global** — no equivalent.
   Justified by REQ-FS-007.
5. **`std::aligned_storage` + placement-new in optional / any holders**
   — generated optionals use a parallel `_present` flag; no any-holder
   exists. Justified by REQ-FS-001 / REQ-FS-006 and MISRA Rule 11.3.
6. **JSON / Service / Pubsub / SQLite / debug-string / walk machinery**
   — out of v1 scope per `01-schema-language.md`.

What we **did** inherit (with attribution):

- The wire format itself (REQ-FN-001 binds us byte-exact).
- The bit-cache + mask-table read pattern.
- The 32/64-bit-base register selection.
- The aligned-EOF probe for `extend` decode.
- The `DeltaContext` algorithm for packed arrays.
- The fixed-buffer-don't-allocate-during-error pattern (in spirit, via
  status codes that carry no payload).

## 10. Acceptance

- [x] Every category of ASIL-blocking pattern in upstream is enumerated
      with citations (§2–§7).
- [x] Upstream's own clang-tidy suppression file is reviewed and
      categorized (§6).
- [x] Things upstream got materially right are credited (§8).
- [x] A concrete safety-case summary is drafted (§9).
- [ ] Each of the 45 throw sites is mapped to a specific
      `ZSERIO_ERR_*` code — done during Phase 1 when the status enum
      is finalized.
