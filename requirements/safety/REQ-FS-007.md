---
id: REQ-FS-007
title: Freedom from interference between codec instances
type: safety
status: accepted
phase_target: 1
verification: analysis
---

## Statement

The runtime library SHALL NOT contain mutable global or `static` storage
visible across multiple codec instances. All state used by encode and
decode operations SHALL be reachable from the caller-provided handle
(`zserio_bitstream_t`, generated-type instance, allocator) passed to the
operation. Two concurrently executing codec operations on disjoint
handles, in different threads, SHALL NOT be able to influence each
other's behavior or observable state.

The runtime is reentrant but not internally thread-safe: a single handle
must be owned by exactly one thread at a time. Synchronization for
shared use is the caller's responsibility and is outside the runtime's
trust boundary.

## Rationale

Freedom from interference is a foundational concept in ISO 26262
(part 9). For a codec library to be usable inside a mixed-criticality
system, the safety case must argue that work performed on one channel
cannot corrupt or stall work performed on another. The simplest way to
make that argument is to ensure the runtime owns no shared state — no
caches, no lazily-initialized singletons, no module-level counters, no
errno-style global. Then the argument reduces to "the caller's handle
is the only state, and the caller controls handle lifetime."

This also makes the runtime trivially reentrant from interrupt context
(provided the caller's handle is not also being touched by the
interrupted code), which several automotive use cases require.

## Verification

- A static analysis check enumerates every `static` / global storage
  symbol exported by the runtime translation units and asserts the set
  is empty (`nm -g`-style check; `readelf -s` filtered by binding).
  Const data (`static const`) is excluded.
- A multi-instance test runs N codec operations on disjoint handles
  concurrently from N threads and asserts each completes with the same
  outputs as a single-threaded reference run.

## Notes

- "Const data" exemption applies to lookup tables (e.g. CRC tables)
  that are read-only and contain no runtime-mutable state. These must
  be marked `static const` and verified by the analysis check.
- Logging hooks, if added later, must take a handle parameter; a
  process-global logger sink would violate this requirement and
  require a deviation.
- Interaction with the standard C library: functions like `memcpy`,
  `memset` are pure and acceptable. Anything that touches `errno`
  (`strtol` etc.) is forbidden in runtime/generated code on this
  ground in addition to REQ-FS-006 (input validation belongs at the
  boundary, not inside conversion helpers).
