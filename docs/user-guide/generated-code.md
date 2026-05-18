# Generated code

What `zserio_c_gen` emits, how it's named, and how to call it.

This is reference material. For the conceptual flow see
[Concepts](concepts.md); for end-to-end usage see
[Getting started](../getting-started.md).

## File layout

For a schema declaring `package pkg.sub;` with a top-level type
`MyType`, the generator emits:

```
<output-dir>/
└── pkg/
    └── sub/
        ├── my_type.h
        └── my_type.c
```

One `.h` / `.c` pair per **top-level type** (struct, choice,
union, enum, bitmask, subtype, instantiate). Inner types nested
inside a templated definition land in the same directory as their
host. Cross-package references are emitted as relative
`#include` paths.

## Naming

The generator follows one consistent convention. Once you've
internalised it every emitted header is predictable.

| Schema | C identifier |
|---|---|
| Type `pkg.sub.MyType` | `pkg_sub_my_type_t` |
| Type entry points | `pkg_sub_my_type_init`, `_set_parameters`, `_read`, `_write`, `_free` |
| Struct field `myField` | `self->my_field` (snake-cased) |
| Optional field `myField` | sibling `bool my_field_present` |
| Variable-length array `things[]` | pair `Thing *things; size_t things_count;` |
| Fixed-size array `things[N]` | `Thing things[N];` inline storage |
| Enum `pkg.Color.RED` | `pkg_COLOR_RED` (package lowercased; type + item kept in schema case) |
| Bitmask same shape | `pkg_PERMISSION_READ_WRITE` |
| Const `pkg.MY_CONST` | `#define pkg_MY_CONST (…)` |
| Choice / union case arm `xyz` | inside an anonymous `union { … } u;` — access as `pkg.choice.u.xyz` |
| Union tag values | `#define <pkg>_<UNION>_TAG_<ARM>` (e.g. `TEST_UNION_TAG_ARRAY8`) |

Snake-case conversion uses a look-ahead rule: insert `_` before
upper-case when the previous character was lower-case or a digit,
OR when the previous was upper-case AND the next is lower-case.
So `UInt8` → `u_int8` and `XMLDocument` → `xml_document`.

### Symbol-length cap

Deep template instantiation chains (e.g. NDS Live's
`AttributeMap<…>` instantiated through five levels of map-of-set-
of-map) can produce identifier names that exceed the filesystem
NAME_MAX (255 bytes per path component). When a mangled name
crosses 160 chars the generator truncates and appends a 16-hex
FNV-1a hash:

```
attribute_map_list_road_..._property_value          → too long
attribute_map_list_road_..._property_…_6ab4420f076d3635   → mapped
```

The hash is deterministic across runs — re-generation produces the
same suffix.

## Lifecycle

Every type follows the same five-step contract. Simple structs use
three of the five; parameterised types add `_set_parameters`;
allocator usage only enters once a type owns variable-length data.

### Decoding

```c
my_type_t pkg;
my_type_init(&pkg);                       /* zero out */
/* parameterised types only: */
my_type_set_parameters(&pkg, /* params… */);

zserio_status_t st = my_type_read(&pkg, &reader, &alloc);
if (st != ZSERIO_OK) {
    /* malformed message; pkg is partially populated but safe to free */
}
/* ...use pkg... */

my_type_free(&pkg, &alloc);               /* release decoder-allocated storage */
```

The decoder takes an allocator because variable-length data —
strings, dynamic arrays, owned `bytes` / `extern` blobs — is
heap-allocated through it. `_free` walks the same fields, releases
each one, and returns the struct to its post-`_init` zero state.

### Encoding

```c
my_type_t pkg;
my_type_init(&pkg);
/* ...populate pkg's fields directly... */

zserio_status_t st = my_type_write(&pkg, &writer);
if (st != ZSERIO_OK) {
    /* writer buffer overflow or programming error */
}
```

The encoder does not allocate, does not need the allocator, and
does not own the bytes you wrote into the struct. If you populated
a string with a pointer into your own buffer, the buffer must
outlive the `_write` call.

After `_write` you decide whether the populated struct needs
freeing — it does only if you populated it via `_read` or if you
manually allocated something through it.

### Parameters

A schema type may declare runtime parameters:

```zserio
struct Item(uint32 version)
{
    uint16 param;
    uint32 extraParam if version > 0;
};
```

The parameter value drives the wire format itself: conditional
fields, dynamic-width bit fields, array lengths. The generator
emits a `_set_parameters` setter that writes parameter values onto
`self->`-prefixed fields:

```c
pkg_item_t item;
pkg_item_init(&item);
pkg_item_set_parameters(&item, /* version = */ 3u);
pkg_item_read(&item, &reader, &alloc);
```

When a parameterised type appears as a field of another type, the
generator chains the inner `_set_parameters` call automatically.
You set the parameter once, on the outer type, and the chain
propagates inward.

## Field shapes

### Required scalar

```zserio
uint32 value;
```

```c
struct { …; uint32_t value; … }
```

### Optional and conditional

```zserio
optional uint32   a;            /* explicit on-wire 1-bit tag */
uint32            b if cond;    /* conditional on expression */
```

Both shapes emit a sibling `bool <name>_present` flag:

```c
struct { …; uint32_t a; bool a_present; uint32_t b; bool b_present; … }
```

For `optional T`, the flag is the wire-format presence bit — you
set it on write, read it back on read.

For `T x if cond`, the predicate is re-evaluated from the
populated fields on write — you populate the field, the writer
decides whether bytes get emitted.

### Arrays

| Schema | Layout |
|---|---|
| `T arr[]` (auto-length) | `T *arr; size_t arr_count;` — caller allocates on write, generator allocates on read |
| `T arr[N]` (fixed-length, `N` is a compile-time constant) | `T arr[N];` — inline storage |
| `T arr[len]` (length expression) | `T *arr; size_t arr_count;` — count comes from the expression at read time |
| `packed T arr[]` | same layout as auto-length; the write side runs an observe pass + per-element delta encode automatically |

Populating an auto-length array for write:

```c
pkg.things_count = 3;
pkg.things = calloc(3, sizeof *pkg.things);
/* ...populate pkg.things[0..2]... */
```

You're responsible for `free`ing arrays you allocated yourself.
`_free` only releases storage that `_read` allocated.

### Choices and unions

```zserio
choice Variant(bool kind) on kind
{
    case true:  uint32 number;
    case false: string label;
};
```

```c
struct {
    bool kind;        /* parameter */
    union { uint32_t number; zserio_string_t label; } u;
}
```

The case arms share storage via an anonymous `union { … } u;`.
Access the active arm through `.u.<name>`. The selector determines
which arm is live.

For zserio **unions** (not choices), the selector is generated as
a `uint32_t tag` field, plus `#define`-style tag macros:

```c
typedef struct {
    uint32_t tag;
    /* per-array count fields outside u { } so they survive arm switches */
    size_t arr_count;
    union { Thing *arr; uint64_t scalar; } u;
} pkg_my_union_t;

#define pkg_my_union_TAG_ARR     0u
#define pkg_my_union_TAG_SCALAR  1u
```

### Extend (forward-compatible fields)

```zserio
struct V2
{
    uint32 value;
    extend uint32 v2Field;
};
```

`extend` is the zserio-2.x evolution annotation. At read time, the
field is decoded only if the bitstream has bytes remaining past
the current position; otherwise the struct field stays at its
zero-initialised default. At write time the field is always
emitted, byte-aligned.

This lets a v2 decoder read a v1 buffer (the `v2Field` reads
trigger the not-enough-bytes path and default to zero) and a v1
decoder read a v2 buffer (the extra bytes are skipped at the
buffer / message boundary).

### Packed compound arrays

`packed T arr[]` where `T` is a struct triggers per-field delta
encoding across the array elements. The generator emits five extra
entry points on `T`:

```c
typedef struct {
    /* one delta context per packable scalar field of T */
    zserio_delta_context_t a_;
    zserio_delta_context_t b_;
    /* nested compound contexts here */
} t_packing_context_t;

void           t_init_packing_context(t_packing_context_t *ctx);
void           t_observe_packing(t_packing_context_t *ctx, const t_t *self);
zserio_status_t t_read_packed(t_t *self, zserio_bitstream_reader_t *bs,
                              const zserio_allocator_t *alloc,
                              t_packing_context_t *ctx);
zserio_status_t t_write_packed(const t_t *self, zserio_bitstream_writer_t *bs,
                              t_packing_context_t *ctx);
```

You don't call these directly. They exist so the enclosing
`packed T arr[]` field's read / write loops can thread a single
context across all elements — that's what makes the delta encoding
work.

## Ownership rules

- `_init` initialises the struct to zero. Always pair with
  `_free` if the struct was populated by `_read` or manually
  through an `alloc`.
- `_read` allocates variable-length data through the supplied
  allocator. The struct now owns those allocations.
- `_write` does **not** allocate, does **not** modify the struct,
  does **not** take an allocator.
- `_free` walks every field, releases what `_read` would have
  allocated, and zeros the struct. Idempotent — safe to call
  twice. Safe to call on a partially-populated struct after a
  failed `_read`.
- Anything you allocated yourself (e.g. `calloc`ing an auto-array
  before `_write`), you free yourself. `_free` doesn't know whose
  storage the pointer came from.

## Status and errors

Every fallible function returns `zserio_status_t`. See
[Error handling](error-handling.md) for the full status taxonomy
and the `ZSERIO_TRY` propagation pattern.

## Worked examples

The 182 harness files under `tests/xvalidation/harnesses/<schema>/c_driver.c`
each populate one schema's top-level type for a deterministic seed,
write the bytes, and dump the count. They're a corpus of small
focused examples per language feature.

For a production-scale read: `conformance/ndslive/read_tile.c`
decodes a real NDS Live 50 KiB tile and round-trips it byte-equal.
