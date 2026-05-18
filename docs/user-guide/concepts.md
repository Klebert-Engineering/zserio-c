# Concepts

The mental model you need to read the rest of the user guide. If
you've used [protobuf](https://protobuf.dev),
[FlatBuffers](https://flatbuffers.dev), or
[Cap'n Proto](https://capnproto.org) some of this will be familiar
shape with different specifics.

## What zserio is

zserio is a schema-driven binary serialization framework. You
describe your data once in the zserio language (`.zs`), and a
generator emits idiomatic code in your target language —
in our case, C99. The generated code provides:

- A C `struct` per schema type, named per a deterministic
  convention.
- A pair of `_read` / `_write` functions per type that move bytes
  between that struct and a zserio-encoded byte stream.
- Helpers for ownership: `_init` (zero) and `_free` (release).

The wire format is **bit-packed**, not byte-aligned. A field
declared as `bit:5` consumes exactly five bits. This makes the
format compact but means you cannot pick out individual fields by
offset without walking the preceding fields.

A 32-byte zserio buffer might pack the same information that
takes 80 bytes in protobuf or 64 bytes in raw JSON.

## The three pieces

```
┌──────────────┐       ┌──────────────┐       ┌──────────────┐
│ your .zs     │──┐    │ generated    │       │ runtime      │
│ schema       │  │    │ <pkg>/foo.h  │       │ libzserio_c  │
└──────────────┘  │    │ <pkg>/foo.c  │       │              │
                  │    └──────┬───────┘       └──────┬───────┘
                  ▼           │                       │
            ┌──────────┐      │                       │
            │ zserio_  │      │                       │
            │ c_gen    │      │                       │
            └──────────┘      │                       │
                              │                       │
                              ▼                       ▼
                            ┌─────────────────────────────┐
                            │ your app.c                  │
                            │   #include "<pkg>/foo.h"    │
                            │   <pkg>_foo_t pkg;          │
                            │   <pkg>_foo_read(...);      │
                            └─────────────────────────────┘
```

- **The generator** (`zserio_c_gen`) is a build-time tool. It runs
  once per schema change and produces a tree of `.h` / `.c` files
  that mirror the schema's package layout.
- **The runtime** is a small C99 library (≈ 1 500 lines under
  `runtime/src/`) that the generated code calls. Bitstream
  primitives, varint codecs, IEEE-754 helpers, packed-array delta
  context, allocator vtable.
- **Your app** includes the generated headers and the runtime
  headers, and links against both compiled together. There is no
  shared library — just two source trees you compile and link.

## Packages

A schema file declares a `package`:

```zserio
package my.app.messages;

struct Header { … };
```

The package becomes:

- A directory in the generator's output (`my/app/messages/header.h`)
- A prefix on every emitted identifier (`my_app_messages_header_t`)
- The unit of `import` between schema files

Packages keep names unambiguous when you compose schemas across
teams. They don't affect the wire format — bytes on the wire are
the same whether the type lives in `pkg.foo` or top-level.

## A schema commits you to wire layout

Zserio's wire format is **driven directly by the schema**.
Fields are written in source order. There is no per-field tag
prefix on the wire (unlike protobuf), no length-prefix on the
struct (unlike FlatBuffers), and no separate descriptor pass.

This makes encoding compact but means **a schema change is a
wire-format change**. Adding a field, reordering fields, or
changing a type are all breaking changes for encoders or decoders
that ship the old schema.

The zserio language has explicit constructs for forward
compatibility (`extend`, `optional`, schema instantiation) and
their wire-level semantics are part of the language definition.
See [Schema language](schema-language.md).

The integration consequence is in
[`safety-case/hazard-analysis.md`](../safety-case/hazard-analysis.md)
H-05: pin one schema version per channel and coordinate
deployments.

## What a zserio buffer looks like

Given the schema:

```zserio
package demo;

struct Point
{
    int16  x;
    int16  y;
    string label;
};
```

And populated values `{x = -3, y = 7, label = "ok"}`, the wire
bytes are:

```
ff fd  00 07  02  6f 6b
└─┬─┘  └─┬─┘  ┬─  └─┬─┘
  │      │   │     └─ label payload (UTF-8 bytes 'o','k')
  │      │   └─────── varsize length prefix (2 bytes follow)
  │      └─────────── y as int16 big-endian
  └────────────────── x as int16 big-endian (sign-extended -3)
```

Seven bytes total. There is no wire structure beyond what the
schema imposes — no headers, no tags, no padding (except where
the schema asks for explicit alignment).

## Read path vs write path

The runtime is symmetric: every encoder has a matching decoder and
the pair is round-trip byte-equal (gated by the
[xvalidation harness](../../tests/xvalidation/)).

```
┌────────────┐      ┌────────────────────┐      ┌────────────┐
│ struct     │      │ bitstream_writer   │      │ uint8_t[]  │
│ T          │─────▶│   T_write(&t, &w)  │─────▶│            │
│            │      └────────────────────┘      │            │
│            │                                  │   wire     │
│            │      ┌────────────────────┐      │            │
│            │◀─────│ bitstream_reader   │◀─────│            │
│            │      │   T_read(&t, &r,   │      │            │
└────────────┘      │           &alloc)  │      └────────────┘
                    └────────────────────┘
```

The encoder is **pure** — it reads from your struct and writes
bytes. No allocation. The decoder may allocate (for variable-length
data: strings, arrays, dynamic bytes blobs), so it takes an
explicit `zserio_allocator_t *`. You're responsible for calling
`T_free` to release whatever the decoder allocated.

This split is the safety story in one diagram: encoders are
deterministic, decoders are explicit about every byte of memory
they consume. See [Allocators](allocators.md) for the supported
allocator backends.

## Where to go next

If you haven't yet, walk the [getting-started example](../getting-started.md).

If you're sizing zserio against alternatives, the
[upstream zserio README](https://github.com/ndsev/zserio) carries
the canonical comparison vs other binary formats.

If you're already encoding/decoding and want the C-side
conventions: [Generated code](generated-code.md).
