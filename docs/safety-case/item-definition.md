---
work_product: ISO 26262-3 §5 — Item definition
status: draft
---

# Item definition — `zserio-c`

## What `zserio-c` is

`zserio-c` is a C99 implementation of the zserio binary serialization
format and IDL. It comprises:

1. A **runtime library** (`runtime/`, library name `libzserio_c_runtime.a`)
   that provides bitstream reading and writing, primitive codecs
   (`uintN`, `intN`, `varintN`, `varuintN`, `varsize`, `bit:N`,
   `float16/32/64`, strings, byte arrays, externs), packed-delta array
   encoding, and the supporting machinery (status codes, allocator
   vtable, alignment helpers).

2. A **Java code generator extension** (`generator/`, `zserio_c.jar`)
   that runs as a plug-in to the upstream `ndsev/zserio` compiler at
   the pinned version `v2.18.1`. From a zserio `.zs` schema it emits
   matching C99 sources containing struct definitions, init/free/read/
   write functions, and packing-context helpers.

3. Generated user code per schema, emitted by the generator. The
   generated code calls only the runtime library and the caller-
   supplied allocator. It contains no static state and no implicit
   allocation.

## Boundary of the item

| In scope                                      | Out of scope                                       |
| --------------------------------------------- | -------------------------------------------------- |
| The runtime library                           | The application using the generated codec         |
| The Java generator extension                  | The toolchain hosting the generator (JDK, OS)     |
| Generated C sources at fixed configuration    | The C compiler, linker, target hardware           |
| The cppcheck MISRA addon configuration in CI  | The cppcheck tool itself                          |
| Wire-format compatibility with zserio v2.18.1 | Upstream zserio's own correctness                 |
| Allocator vtable contract                     | Concrete allocator implementations supplied by integrators |
| Status-code error reporting                   | Application-level recovery from those status codes |

## Assumed system context

`zserio-c` is intended to be linked into an automotive ECU software
stack as a serialization layer. The expected operational context:

- A surrounding application that opens, reads, or writes byte streams
  (for example: NDS.Live map-data messages, vehicle-network frames,
  file payloads).
- A caller-supplied allocator. The integrator chooses between the
  reference `libc` wrapper (`runtime/src/allocator.c`) and a fixed-pool
  implementation suited to safety builds. The integrator's choice
  shapes the memory-bound analysis.
- An external integrity layer where end-to-end protection is required.
  Zserio's wire format does not include checksums, CRCs, or message
  authentication codes — see HARA hazard H-04.
- Schema authorship and review are performed at integration time.
  Schemas are part of the trusted input set and are not subject to
  runtime validation beyond what zserio's compiler enforces at
  generation time.

## Operational modes

The runtime exposes one operational mode: encode/decode of values
against a generated schema, driven by a `zserio_bitstream_reader_t`
or `zserio_bitstream_writer_t`. There is no separate "trusted" mode
distinct from a "normal" mode. The trust boundary is the public API:
every entry point validates its arguments (REQ-FS-006).

The optional `zsc_trace_*` facility (`runtime/src/bitstream.c`, gated
behind `-DZSERIO_TRACE_ENABLED`) is a developer-time diagnostic only.
It uses `<stdio.h>` and `getenv`, both MISRA-banned, and is excluded
from production builds. See `docs/misra-deviations/` and `tools/run_misra.sh`
for the enforcement mechanism.

## Safety-relevant claims that the item makes

- All allocation is caller-routed (REQ-FS-001).
- No exception-style control flow (REQ-FS-002).
- No recursive call chains (REQ-FS-003).
- No floating-point in control flow (REQ-FS-004).
- All loops are bounded by statically determinable upper bounds
  (REQ-FS-005).
- Every public entry point validates its inputs (REQ-FS-006).
- Codec instances cannot influence each other through shared mutable
  state (REQ-FS-007).
- Generated code contains no runtime reflection (REQ-FS-008).
- Wire bytes produced are byte-equal to upstream zserio v2.18.1
  (REQ-FN-001).

## Versions and pins

- Upstream zserio: submodule `third_party/zserio` at tag `v2.18.1`.
- Language: ISO/IEC 9899:1999 (C99) with MISRA C:2012 + amendments 1–4.
- Toolchain references: gcc and clang in the CI configuration listed
  in `.github/workflows/ci.yml`. These are not part of the item;
  qualification of the chosen production toolchain is the integrator's
  responsibility (with our tool-qualification records as input — see
  `tool-qualification.md`, planned).
