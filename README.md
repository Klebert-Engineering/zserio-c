# zserio-c

A C99 implementation of the [zserio](https://zserio.org) schema-driven
binary serialization framework, with a clean-room parser/generator and
runtime designed for automotive functional-safety use (ISO 26262
ASIL-B target, ASIL-D headroom).

[![CI](https://github.com/ke-fritz/zserio-c-shadow/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/ke-fritz/zserio-c-shadow/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Standard](https://img.shields.io/badge/C-99-informational.svg)](https://www.iso.org/standard/29237.html)
[![Upstream zserio](https://img.shields.io/badge/upstream-v2.18.1-orange.svg)](https://github.com/ndsev/zserio/releases/tag/v2.18.1)
[![Wire compatibility](https://img.shields.io/badge/xvalidation-182%20%2F%20182-success.svg)](tests/xvalidation/)
[![Coverage](https://img.shields.io/badge/coverage-99.9%25%20line%20%C2%B7%2088%25%20branch-success.svg)](docs/safety-case/tool-qualification-coverage.md)
[![MISRA](https://img.shields.io/badge/MISRA--C%3A2012-strict%20%2B%20D001..D006-blue.svg)](docs/misra-deviations/)
[![ISO 26262](https://img.shields.io/badge/ISO%2026262-ASIL--B%20target-blue.svg)](docs/safety-case/)

--------

```zserio
// hello.zs
package hello;

struct Greeting
{
    uint32 messageId;
    string text;
};
```

```c
hello_greeting_t msg;
hello_greeting_init(&msg);
msg.message_id  = 0xCAFEBABEu;
msg.text.bytes  = "hello, world";
msg.text.length = 12;

zserio_bitstream_writer_t w;
zserio_bitstream_writer_init(&w, buf, sizeof buf * 8u);
hello_greeting_write(&msg, &w);   /* 17 bytes on the wire */
```

[Quick start](docs/getting-started.md) walks the full encode + decode
loop end to end in under 10 minutes. The [User guide](docs/user-guide/)
covers the schema language, the generated-code surface, and the
patterns you'll meet when schemas grow.

--------

## What it is

`zserio-c` consists of three pieces, all in this repository:

- **`zserio_c_gen`** — a C-native CLI that parses `.zs` schemas and
  emits C99 code against the runtime. No JVM, no Java jar dependency,
  no FreeMarker templates. The output compiles under
  `-std=c99 -Wall -Wextra -Wpedantic -Werror` on every supported
  compiler.
- **`libzserio_c_runtime`** — the small C99 library generated code
  links against. Bitstream primitives, varint, packed-array delta
  contexts, IEEE-754 helpers, allocator vtable.
- **Generated code** — one `.h` / `.c` pair per top-level type in
  your schema, plus matching headers for the imports it pulls in.
  Wire-compatible byte-for-byte with upstream `ndsev/zserio v2.18.1`.

## What it is not

- Not a wrapper around the upstream C++ runtime — fully clean-room.
- Not a streaming codec — `zserio-c` decodes against a fully-buffered
  message and encodes into a pre-sized output buffer.
- Not multi-threaded — one reader / writer per thread (REQ-FS-007).
- Not a schema-evolution layer — pin one schema version per channel
  ([H-05](docs/safety-case/hazard-analysis.md)).

## Design pillars

The four are CI-gated on every commit, not aspirational:

| Pillar | Gate |
|---|---|
| Pure C99 generated code | `-std=c99 -pedantic -Werror` plus strict warnings in [`CMakeLists.txt`](CMakeLists.txt) on every supported compiler (clang, gcc). No GNU extensions. |
| No implicit heap | Forbidden-symbol grep blocks any direct `malloc`/`calloc`/`realloc`/`free` in `runtime/src/*.c` or generated code. All allocation flows through `zserio_allocator_t`. |
| MISRA C:2012 strict | `cppcheck --addon=misra` clean on runtime sources AND on generated output from the upstream test corpus. Six documented deviations under [`docs/misra-deviations/`](docs/misra-deviations/). |
| ISO 26262 alignment | 14 work products under [`docs/safety-case/`](docs/safety-case/) grown alongside the code, not retrofitted. |

## Numbers

### Wire compatibility — `xvalidation` gate

`tests/xvalidation/run_xvalidation.sh` encodes 182 schemas × 5 seeds
each through our writer and decodes through upstream's cpp reader,
then re-encodes via upstream's cpp writer and decodes via ours.
**182 / 182 byte-equal in both directions**, on every commit.

### Test coverage — `coverage` gate

Measured by gcov+lcov against the Unity unit-test suite on
`runtime/src/*.c`. Thresholds enforced by
[`tools/check_coverage.py`](tools/check_coverage.py):

| Metric | Threshold | Current |
|---|---:|---:|
| Line | 99.0% | **99.89%** |
| Branch | 85.0% | **88.03%** |
| Function | 100.0% | **100%** |

### Performance — `perf` gate

Median-of-5 decode `ns/op` vs upstream cpp on two pinned hosts. Full
table at [`docs/benchmarks/RESULTS.md`](docs/benchmarks/RESULTS.md).

| Host | Win or tie | Notable arena workload |
|---|---:|---|
| `pi4` (Raspberry Pi 4) | 10 / 14 | `addressbook_align_arena` — **2.08×** ahead of cpp `std::pmr` |
| `primary` (AMD x86_64) | 7 / 14 | `large_array` — **2.29×** ahead of cpp |

The arena allocator (`zserio_allocator_arena`, [`runtime/include/zserio/allocator.h`](runtime/include/zserio/allocator.h))
is the embedded deployment recommendation; see
[`docs/user-guide/allocators.md`](docs/user-guide/allocators.md).

### Stack budget — `stack` gate

Built from gcc's `-fcallgraph-info` graph (no LTO; production builds
with `-flto` measure smaller). Enforced by
[`tools/measure_stack_usage.py --strict`](tools/measure_stack_usage.py):

| Bound | Limit | Current |
|---|---:|---:|
| Per-function frame | ≤ 256 B | **160 B** |
| Worst transitive chain | ≤ 1 024 B | **512 B** |

## Documentation

For **using** zserio-c — schema → C code → linked executable:

| | |
|---|---|
| [Getting started](docs/getting-started.md) | 10-minute walkthrough, zero-to-running. |
| [Concepts](docs/user-guide/concepts.md) | Mental model: schemas, packages, wire format. |
| [Schema language tutorial](docs/user-guide/schema-language.md) | The supported `.zs` subset, friendly intro. |
| [Generated code](docs/user-guide/generated-code.md) | Naming, lifecycle, ownership. |
| [Allocators](docs/user-guide/allocators.md) | libc, arena, custom. |
| [Error handling](docs/user-guide/error-handling.md) | `zserio_status_t` and `ZSERIO_TRY`. |
| [Build integration](docs/user-guide/build-integration.md) | CMake / Make snippets. |
| [Patterns](docs/user-guide/patterns.md) | Round-trip, message reuse, schema composition. |
| [`zserio_c_gen` CLI](docs/cli.md) | Flag reference for the generator. |

For **implementing** an interoperable zserio codec, or **auditing the wire format**:

| | |
|---|---|
| [Schema language reference](docs/spec/schema-language.md) | Normative spec of the language `zserio_c_gen` accepts. |
| [Wire format reference](docs/spec/wire-format.md) | Normative spec of the binary encoding. |
| [Schema evolution](docs/spec/schema-evolution.md) | `optional` / `extend` / `@removed` semantics. |
| [Types mapping](docs/spec/types-mapping.md) | Schema → C type table. |
| [Quick reference](docs/spec/quick-reference.md) | Cheat sheet covering every construct. |
| [Compatibility policy](docs/spec/compatibility.md) | Five-axis versioning policy. |

For **working on** zserio-c, or **deploying it under ISO 26262**:

| | |
|---|---|
| [Contributing](docs/contributing.md) | Build, test, MISRA, perf. |
| [Architecture](docs/architecture.md) | Internal design. Load-bearing. |
| [Requirements](requirements/) | Domain, functional, safety, and performance requirements. |
| [Safety case](docs/safety-case/) | 14 ISO 26262 work products. |
| [Integration responsibilities](docs/integration-guide.md) | What the safety case delegates to the embedding system. |
| [MISRA deviations](docs/misra-deviations/) | D001..D006 with justifications. |
| [Upstream analysis](docs/upstream-analysis/) | Six deep-reads of zserio that motivated v1 design. |

## Repository layout

```
parser/                  zserio_c_gen — C-native parser/sema/emitter
runtime/                 libzserio_c_runtime — C99 source + headers
generator/               legacy Java generator extension (Phase 2)
third_party/zserio/      upstream submodule, pinned at v2.18.1

tests/runtime/           Unity unit tests
tests/xvalidation/       182-schema byte-equal cross-validation harness
tests/benchmarks/        REQ-PF-001 throughput harness vs upstream cpp
conformance/ndslive/     NDS Live SmartLayerTile end-to-end fixture

requirements/            REQ-FN-* (functional), REQ-FS-* (safety),
                         REQ-PF-* (performance) — one .md per requirement
docs/                    See "Documentation" above
examples/                Recommended deployment patterns

tools/                   measure_*, check_*, render_*, run_misra,
                         build_*_jar
.github/workflows/ci.yml The CI gates listed in "Numbers"
```

## Versioning

`zserio-c` tracks upstream `ndsev/zserio` at a single pinned version
(currently `v2.18.1`). The submodule pin is the single source of
truth for wire-format compatibility.

There is no public ABI yet — we ship the C99 source. Consumers
vendor or submodule the runtime and rebuild from source against
their chosen toolchain. This is intentional for safety integrators
who need to qualify the toolchain end to end.

## License

[MIT](LICENSE). Copyright © 2026 Fabian Klebert.

The upstream `ndsev/zserio` submodule retains its own license
(Apache 2.0). No upstream source is copied into this repository —
the submodule is consulted at build time only for the Java
generator extension (`generator/`) and at xvalidation time for the
cpp wire-format oracle.
