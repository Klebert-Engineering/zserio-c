# zserio-c

ANSI C port of the [zserio](https://github.com/ndsev/zserio) schema IDL,
designed from day one for use in automotive functional-safety systems
(ISO 26262, target ASIL-B, headroom toward ASIL-D).

## Status

**Phase 0 — Bootstrap.** The repository scaffold, requirements seed, and
upstream submodule are in place. No runtime or generator code exists yet.

See [`docs/architecture.md`](docs/architecture.md) for the design intent
and the phase plan.

## Project goals

- **Pure C99 generated code** — no C++, no runtime polymorphism, no exceptions.
  Bindings to higher-level languages are downstream concerns.
- **No implicit heap** — runtime and generated code never call `malloc`/`free`
  directly. All allocation goes through a caller-supplied allocator vtable.
  The default backend is `malloc`; safety builds plug in a static pool.
- **MISRA C:2012 clean** with documented, justified deviations.
- **ISO 26262 alignment** from the first commit: requirements, traceability,
  verification plan, safety case grow alongside the code, not after.
- **Full line and branch coverage** of the runtime and generated code,
  enforced in CI.
- **Wire-compatible** with upstream `ndsev/zserio` at the pinned version.
- **Faster** than the upstream C++ runtime on read and write throughput.

## Why a port

The existing C++ generator and runtime are not viable for automotive
functional-safety production: they rely on dynamic allocation, exceptions,
RTTI and STL containers whose memory and timing behavior are difficult to
bound and certify. A from-scratch C99 port lets us control these properties
by design instead of retrofitting them.

## Repository layout

```
runtime/         libzserio-c — the C runtime (encoders, bitstream, allocator)
generator/       Java extension that plugs into the upstream zserio toolchain
                 and emits C against the runtime
third_party/     zserio submodule (ndsev/zserio @ v2.18.1)
docs/            architecture, safety case, coding rules, upstream analysis
requirements/    REQ-FN-* (functional), REQ-FS-* (safety), REQ-PF-* (perf)
tests/           Unity-based runtime tests, generated round-trip tests,
                 conformance against upstream test schemas
tools/           coverage, MISRA, requirements traceability scripts
examples/        end-to-end demos
```

## License

MIT — see [`LICENSE`](LICENSE). Upstream zserio under `third_party/zserio/` is
BSD-3-Clause; that license applies to its contents.
