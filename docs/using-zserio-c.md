# Using zserio-c

This is the integrator-facing user guide for the `zserio-c` parser /
generator and the C99 code it emits. If you have a `.zs` schema and
want to read or write zserio-encoded bytes from a C program, start
here.

For the *design* of zserio-c see [`architecture.md`](architecture.md).
For the *integration responsibilities* the safety case delegates to
the embedding system (allocator choice, stack sizing, integrity
wrapping) see [`integration-guide.md`](integration-guide.md). This
document is the day-one walkthrough.

## Quickstart

A 10-minute end-to-end: schema → C → executable → byte-equal
round-trip.

### 1. Build `zserio_c_gen` and the runtime

```sh
cmake -S . -B build
cmake --build build -j
```

This produces:

- `build/parser/zserio_c_gen` — the CLI generator
- `runtime/include/zserio/*.h` — public runtime headers
- `runtime/src/*.c` — runtime sources (you compile + link these
  alongside your generated code)

### 2. Write a tiny schema

```zserio
// hello.zs
package hello;

struct Greeting
{
    uint32 messageId;
    string text;
};
```

### 3. Generate C code

```sh
mkdir -p out
build/parser/zserio_c_gen hello.zs -c out
```

This writes:

```
out/
└── hello/
    ├── greeting.h
    └── greeting.c
```

The generated functions you'll call are:

| Symbol | What it does |
|---|---|
| `hello_greeting_t` | The struct typedef |
| `hello_greeting_init(self)` | Zero-initialise (call before `_read` or before populating to write) |
| `hello_greeting_read(self, bs, alloc)` | Decode from a bitstream reader |
| `hello_greeting_write(self, bs)` | Encode to a bitstream writer |
| `hello_greeting_free(self, alloc)` | Free anything `_read` allocated |

### 4. Use it from C

```c
// app.c — encode a Greeting, then decode it back.
#include <stdio.h>
#include <string.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "hello/greeting.h"

int main(void)
{
    const zserio_allocator_t alloc = zserio_allocator_libc();

    /* ---- encode ---- */
    hello_greeting_t out_msg;
    hello_greeting_init(&out_msg);
    out_msg.message_id = 0xCAFEBABEu;
    /* zserio strings are a view + length pair; the writer never
     * copies the bytes, just reads them out. */
    const char text[] = "hello, world";
    out_msg.text.bytes  = (char *)text;
    out_msg.text.length = sizeof text - 1u;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof buf * 8u);
    if (hello_greeting_write(&out_msg, &w) != ZSERIO_OK) return 1;
    const size_t n_bytes =
        (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    printf("encoded %zu bytes\n", n_bytes);

    /* ---- decode ---- */
    hello_greeting_t in_msg;
    hello_greeting_init(&in_msg);
    zserio_bitstream_reader_t r;
    zserio_bitstream_reader_init(&r, buf, n_bytes * 8u);
    if (hello_greeting_read(&in_msg, &r, &alloc) != ZSERIO_OK) {
        return 1;
    }
    printf("messageId=0x%08x text='%.*s'\n",
           in_msg.message_id,
           (int)in_msg.text.length, in_msg.text.bytes);

    hello_greeting_free(&in_msg, &alloc);
    return 0;
}
```

### 5. Compile and link

```sh
gcc -std=c99 -Wall -Wextra -Iruntime/include -Iout \
    app.c out/hello/greeting.c runtime/src/*.c \
    -o app
./app
```

Expected output:

```
encoded 17 bytes
messageId=0xcafebabe text='hello, world'
```

That's the whole loop. The rest of this guide explains the patterns
you'll meet when schemas grow beyond `Greeting`.

---

## The `zserio_c_gen` CLI

```
zserio_c_gen <input.zs> -c <output-dir> [-src <root>...]
                        [-setTopLevelPackage <pkg>]
```

| Flag | Meaning |
|---|---|
| `<input.zs>` | The top-level schema file. Its `import` graph is followed transitively. |
| `-c <output-dir>` | Where to write `.c` / `.h` files. Sub-directories mirror the package path. |
| `-src <root>` | Search root for `import` resolution. Repeatable. Defaults to the directory containing `<input.zs>`. |
| `-setTopLevelPackage <pkg>` | Prepend `pkg` to every loaded unit's `package` declaration. Used to dodge identifier collisions with libc / language keywords (the canonical case: a schema declaring `package index;` clashes with `index()` from `<strings.h>` on a cpp side). Matches upstream zserio's flag of the same name. |

Imports are resolved by translating dotted package names to
filesystem paths. `import a.b.c.*` is looked up as `<root>/a/b/c.zs`,
which is the conventional one-package-per-file layout zserio uses.

The generator is deterministic given the same inputs; rerunning
produces byte-identical output. It does not modify files outside
`-c <output-dir>`.

---

## Naming rules

The generator follows one consistent convention across types,
fields, parameters, and enum / bitmask items. Once you've internalised
it, every header is predictable.

| Schema | C identifier |
|---|---|
| Type `pkg.sub.MyType` | `pkg_sub_my_type_t` |
| Type entry points | `pkg_sub_my_type_init`, `_read`, `_write`, `_free`, `_set_parameters` |
| Struct field `myField` | `self->my_field` (snake-cased) |
| Optional field `myField` | sibling `bool my_field_present` |
| Variable-length array `things[]` | pair `Thing *things; size_t things_count;` |
| Fixed-size array `things[N]` | `Thing things[N];` (inline storage) |
| Enum `pkg.Color.RED` | `pkg_COLOR_RED` (package lowercased, type + item kept in schema case) |
| Bitmask same shape | `pkg_PERMISSION_READ_WRITE` |
| Const `pkg.MY_CONST` | `#define pkg_MY_CONST (…)` |
| Choice / union case arm `xyz` | inside an anonymous `union { … } u;` on the struct — access as `pkg.choice.u.xyz` |
| Union tag values | `#define <pkg>_<UNION>_TAG_<ARM>` (e.g. `TEST_UNION_TAG_ARRAY8`) |

`snake_case` is applied with a look-ahead rule: insert `_` before an
upper-case letter when the previous character was lower-case or a
digit, OR when the previous was upper-case AND the next is
lower-case (so `UInt8` → `u_int8` and `XMLDocument` → `xml_document`).

---

## Lifecycle: init / set_parameters / read / write / free

Every non-trivial generated type follows the same five-step contract.
For a simple struct only three matter; parameterised types add
`_set_parameters`; the runtime allocator only enters once you have a
type that owns anything heap-shaped (arrays, strings, bytes, owned
sub-compounds).

### Decoding (`_read`)

```c
my_type_t pkg;
my_type_init(&pkg);                    /* zero out */
/* (parameterised types only) — supply runtime parameter values */
my_type_set_parameters(&pkg, /* params… */);
if (my_type_read(&pkg, &reader, &alloc) != ZSERIO_OK) {
    /* malformed message; pkg is partially populated but safe to free */
}
/* …use pkg… */
my_type_free(&pkg, &alloc);            /* releases anything _read allocated */
```

The reader takes an allocator because variable-length data
(strings, bytes, dynamic arrays) is heap-allocated through it.
`_free` walks the same fields and releases each, returning the
struct to its post-`_init` zero state.

### Encoding (`_write`)

```c
my_type_t pkg;
my_type_init(&pkg);
/* …populate pkg's fields directly (and `_set_parameters` if needed)… */
if (my_type_write(&pkg, &writer) != ZSERIO_OK) {
    /* writer buffer overflow or programming error */
}
```

The writer does not allocate, does not need the allocator, and does
not own the bytes you wrote into the struct. If you populated `text`
with a pointer into your own buffer, the buffer must outlive the
`_write` call. After `_write` you decide whether the populated
struct needs freeing — it does only if you populated it via `_read`
or via a manual `*_alloc`-style call you made.

### Parameterised types (`_set_parameters`)

```zserio
struct Item(uint32 version)
{
    uint16 param;
    uint32 extraParam if version > 0;
};
```

```c
pkg_item_t item;
pkg_item_init(&item);
pkg_item_set_parameters(&item, /* version = */ 3u);
pkg_item_read(&item, &reader, &alloc);
```

Runtime parameters are values used by the wire format itself
(conditional fields, dynamic-width bit-fields, array lengths…).
`_set_parameters` writes them into `self->`-prefixed parameter
fields on the struct; the read / write loops then reference those.

When a parameterised type appears as a field of another type, the
generator wires up the inner `_set_parameters` call automatically —
you don't repeat it manually.

### Optional and conditional fields

```zserio
struct Holder
{
    bool   hasExtra;
    uint32 extra if hasExtra == true;
};
```

The generator emits a sibling `bool extra_present` flag. On read,
the flag mirrors the predicate. On write, the predicate is re-
evaluated from the populated fields — you populate `hasExtra` and
`extra`, the writer decides whether bytes get emitted.

For `optional T x;` (the unconditional-optional form), the
`_present` flag is the explicit on-wire 1-bit tag; you set it before
write and read it back after read.

### Arrays

| Shape | Layout |
|---|---|
| `T arr[]` (auto-length) | `T *arr; size_t arr_count;` — caller allocates on write, generator allocates on read |
| `T arr[N]` (fixed) | `T arr[N];` inline storage |
| `T arr[len]` (length-expr) | `T *arr; size_t arr_count;` — count comes from the length expression at read time |
| `packed T arr[]` | same layout as auto-length; the WRITE side runs an observe pass + per-element delta encode automatically |

Populating an auto-length array for write:

```c
pkg.things_count = 3;
pkg.things = calloc(3, sizeof *pkg.things);
/* …populate pkg.things[0..2]… */
```

You're responsible for `free`'ing arrays you allocated yourself.
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
my_variant_t v;
my_variant_init(&v);
my_variant_set_parameters(&v, /* kind = */ true);
v.u.number = 42;
my_variant_write(&v, &writer);
```

The case arms share storage via an anonymous `union { … } u;`.
Access the active arm through `.u.<name>`. The selector value
determines which arm is live; for unions, the selector is the
generated `tag` field (`uint32_t`) and named-constant macros
(`<pkg>_<UNION>_TAG_<ARM>`) give you the tag values without
counting case ordinals.

### Packed arrays of compound elements

If you write `packed T arr[]` where `T` is a compound type, the
generator also emits `T`'s `_packing_context_t`, plus
`T_init_packing_context`, `T_observe_packing`,
`T_read_packed`, `T_write_packed`. You don't call these directly —
they're emitted so the enclosing array's read / write loops can
thread a context across all elements for per-field delta encoding.

The only time you might touch them yourself is if you write a
custom container type by hand; the generated code uses them
internally.

---

## Allocators

Every public `_read` and `_free` takes a `const zserio_allocator_t *`.
Three options:

```c
/* libc malloc / free. Good for development. */
const zserio_allocator_t alloc = zserio_allocator_libc();

/* Bounded bump arena backed by a fixed pool. The arena's
 * high_water field tells you the worst case you observed. */
static uint8_t pool[64 * 1024];
zserio_arena_t arena;
zserio_arena_init(&arena, pool, sizeof pool);
const zserio_allocator_t alloc = zserio_allocator_arena(&arena);
/* …decode several messages… */
zserio_arena_reset(&arena);  /* O(1) reclaim between channels */

/* Your own vtable. Three function pointers: alloc / free / realloc.
 * See runtime/include/zserio/allocator.h for the contract. */
const zserio_allocator_t alloc = { .vtable = &my_vtable,
                                   .userdata = &my_pool_state };
```

See [`integration-guide.md`](integration-guide.md) § 1 for the
trade-offs between these in a safety-relevant context.

---

## Error handling

Every fallible function returns a `zserio_status_t`. There are no
exceptions, no `errno`, no thread-local state. The common pattern:

```c
zserio_status_t st = my_type_read(&pkg, &r, &alloc);
if (st != ZSERIO_OK) {
    /* st says what kind of failure; pkg may be partially built;
     * call _free safely either way. */
    my_type_free(&pkg, &alloc);
    return st;
}
```

The runtime ships a `ZSERIO_TRY(expr)` macro for the common case of
"propagate the status on failure":

```c
ZSERIO_TRY(zserio_read_uint32(bs, &n));
ZSERIO_TRY(my_inner_read(&pkg.inner, bs, alloc));
return ZSERIO_OK;
```

Status codes split into three groups:

- **`ZSERIO_OK`** — proceed.
- **Bad input** — `_END_OF_STREAM`, `_INVALID_TAG`, `_INVALID_ENCODING`,
  `_VALUE_OUT_OF_RANGE`. The message is malformed for the schema you
  decoded against; the application decides whether to drop, log, or
  request retransmission.
- **Programming error / environment** — `_INVALID_ARGUMENT`,
  `_INVALID_BUFFER`, `_INVALID_POSITION`, `_BUFFER_OVERFLOW`,
  `_OUT_OF_MEMORY`. The first three indicate the caller passed a
  malformed argument (writer buffer too small, NULL pointer where
  one was required). `_OUT_OF_MEMORY` means the allocator returned
  NULL.

The full list with semantics: `runtime/include/zserio/status.h`.

---

## Bitstream primitives

`_read` and `_write` are layered over a small bitstream API. You'll
touch it once per message — to initialise a reader over your input
bytes, or a writer over an output buffer:

```c
/* reader over a fully-buffered message */
zserio_bitstream_reader_t r;
zserio_bitstream_reader_init(&r, buf, n_bytes * 8u);

/* writer over a pre-sized output buffer */
zserio_bitstream_writer_t w;
zserio_bitstream_writer_init(&w, out_buf, capacity_bytes * 8u);
/* …call _write(&pkg, &w)… */
const size_t bit_pos   = zserio_bitstream_writer_position(&w);
const size_t byte_size = (bit_pos + 7u) / 8u;
```

The reader validates every read against the bit-size you passed —
it cannot run off the end of `buf`. The writer fails fast with
`ZSERIO_ERR_BUFFER_OVERFLOW` if you under-sized the output buffer.

Sizing the output buffer ahead of time: the worst case is bounded
by your schema. For ad-hoc use, oversize generously (e.g. `4 *
input_size + 1KiB`) and trim by `byte_size` after writing.

---

## Build integration

There's no zserio-c shared library to link. Two pieces go into your
build:

1. The generated `<your_schema>/*.c` files under `-c <output-dir>`.
2. The runtime `runtime/src/*.c` files.

Both compile under `-std=c99 -Wall -Wextra -Wpedantic -Werror` and
depend only on the C standard library. Include paths:

- `-Iruntime/include` for the `<zserio/...>` headers.
- `-I<output-dir>` for the generated headers.

Example CMake snippet:

```cmake
# 1. regenerate when the schema changes
add_custom_command(
    OUTPUT  ${GENERATED_OUT}/${SCHEMA_STAMP}
    COMMAND zserio_c_gen ${CMAKE_SOURCE_DIR}/schemas/all.zs
                          -c ${GENERATED_OUT}
                          -src ${CMAKE_SOURCE_DIR}/schemas
    COMMAND ${CMAKE_COMMAND} -E touch ${GENERATED_OUT}/${SCHEMA_STAMP}
    DEPENDS schemas/all.zs)

# 2. collect the generated and runtime sources
file(GLOB_RECURSE GEN_SRCS ${GENERATED_OUT}/*.c)
file(GLOB         RT_SRCS  ${CMAKE_SOURCE_DIR}/runtime/src/*.c)

add_library(my_zserio STATIC ${GEN_SRCS} ${RT_SRCS}
                            ${GENERATED_OUT}/${SCHEMA_STAMP})
target_include_directories(my_zserio PUBLIC
    ${CMAKE_SOURCE_DIR}/runtime/include
    ${GENERATED_OUT})
```

For Bazel / Make / your-build-system, the shape is the same: run
the generator as a build step that depends on the `.zs` files;
compile every emitted `.c` plus every runtime `.c` as a single
unit.

---

## Worked examples

The cross-validation harness under `tests/xvalidation/harnesses/`
has 182 small self-contained C programs that each exercise one
schema. `simple_structure/c_driver.c` and
`simple_union/c_driver.c` are good places to start — they show the
populate-and-write side of every supported language feature in
~30 lines per file.

For a production-scale example, see
`conformance/ndslive/read_tile.c`: 320 lines, decodes a real
NDS Live SmartLayerTile, dispatches a polymorphic layer
collection by name, and round-trips byte-identical against the
original 50 KiB upstream-encoded fixture.

---

## What's NOT covered here

These are intentionally out of this document; pointers given.

| Topic | Where |
|---|---|
| Safety case, hazard analysis, integration responsibilities | [`integration-guide.md`](integration-guide.md), [`safety-case/`](safety-case/) |
| Wire-format spec | [`upstream-analysis/02-wire-format.md`](upstream-analysis/02-wire-format.md) |
| Internal architecture / design decisions | [`architecture.md`](architecture.md) |
| Generated-code MISRA deviations | [`misra-deviations/`](misra-deviations/) |
| Performance benchmarks | [`benchmarks/`](benchmarks/) |
| Setting up self-hosted CI | [`ci-self-hosted.md`](ci-self-hosted.md) |
