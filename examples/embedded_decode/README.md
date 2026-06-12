# `examples/embedded_decode` — arena-backed decode demo

A minimal end-to-end example showing how an embedded ECU caller
deploys `zserio-c`:

1. A small caller-owned static buffer backs a bump arena.
2. The arena is plugged into the runtime via the `zserio_allocator_t`
   vtable (REQ-FS-001 — no libc malloc on the safety path).
3. The example decodes a hand-built wire blob, prints the decoded
   fields, and reports the worst-case arena watermark so the
   integrator can size their pool from measured evidence.

This is the recommended deployment pattern for ASIL-relevant
contexts. The libc-backed allocator (`zserio_allocator_libc()`) is
fine for development and host-side tooling; production sites
typically want the arena, a custom pool, or a `pmr`-style allocator
of their own.

## Files

| File             | Role                                               |
| ---------------- | -------------------------------------------------- |
| `telemetry.zs`   | Minimal schema: a sensor frame with a label + samples. |
| `main.c`         | Decoder. Sets up the arena, decodes one frame, prints, reports the watermark. |
| `CMakeLists.txt` | Build glue. Invokes the generator if `gen/` is empty, then links the runtime + generated TUs + `main.c`. |

## Build + run

```bash
# No prerequisites — the generated code under gen/ is committed, so
# the build is hermetic (no generator run, no JVM).

# In this directory:
cmake -B build -S .
cmake --build build
./build/embedded_decode
```

To regenerate `gen/` after editing `telemetry.zs`, build the
C-native generator once and run it (from this directory):

```bash
cmake --preset gcc-release --fresh   # repo root
cmake --build --preset gcc-release --target zserio_c_gen
./../../build/gcc-release/parser/zserio_c_gen telemetry.zs -c gen
```

Expected output:

```
sensor_id     = 0x01020304
device_label  = "engine_temp"
reading_count = 4
readings      = 21 22 23 -1
arena high_water = 20 / 4096 bytes
```

The `arena high_water` line is the diagnostic an integrator uses to
size the static pool — run the example on representative worst-case
inputs and pick a pool size with margin above the maximum
`high_water` observed.

## Why an arena

Embedded automotive callers typically can't ship libc `malloc`:

- The heap is global and shared across modules, which violates the
  freedom-from-interference argument the safety case relies on.
- Libc `malloc` is not a qualified ASIL component on most automotive
  toolchains.
- Per-allocation cost is high (the addressbook benchmark profile
  shows ~21K alloc/free pairs per decode, dominating the runtime).
- Fragmentation across long-running services is hard to bound.

The bump arena fixes all four:

- Owned by the caller; no global state.
- A pointer add per `alloc` and a no-op `free` — trivially
  qualifiable, and ~3.4–3.7× faster than libc on the addressbook
  benchmark (perf gate evidence: `addressbook_align_arena` workload).
- Caller picks the buffer's storage class (e.g. on-die SRAM via a
  linker-script section).
- O(1) reset between messages — no fragmentation.

The downside is a deployment constraint: every owned pointer the
generated reader hands back is borrowed from the arena, so the
caller must consume the decoded value before the next
`zserio_arena_reset()`. For the streaming-decode pattern most ECUs
use this is natural.

## Cross-references

- `runtime/include/zserio/allocator.h` — allocator vtable + arena API.
- `docs/integration-guide.md` § "Supply an allocator" — the
  integrator-facing rundown of which allocator to pick.
- `docs/benchmarks/methodology.md` § "Allocator choice in the perf
  gate" — why the arena perf row reports cpp/c > 1, what it means
  and what it doesn't.
- `requirements/safety/REQ-FS-001.md` — the no-implicit-heap rule
  this pattern is built to satisfy.
