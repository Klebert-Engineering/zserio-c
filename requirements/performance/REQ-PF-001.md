---
id: REQ-PF-001
title: Throughput at least matching upstream C++ runtime
type: performance
status: accepted
phase_target: 6
verification: test
---

## Statement

On a defined benchmark schema and host, single-threaded read and write
throughput of the zserio-c runtime, measured in bytes per second, SHALL
meet or exceed the throughput of the upstream `ndsev/zserio` C++ runtime
at the pinned version, both compiled with equivalent optimization flags
(`-O2` minimum).

## Rationale

A from-scratch C99 port that is *slower* than the C++ original is hard
to justify, especially given the additional design constraints (no STL,
status codes instead of exceptions, allocator vtable indirection). We
explicitly want to win on this axis — the design pillars are intended to
be performance-positive (no allocator surprises, no exception overhead,
inlinable status checks), so this requirement holds us to that thesis.

## Verification

- A benchmark harness (added in Phase 1, exercised in Phase 6) measures
  read and write throughput for:
  - A small flat struct (10 fixed-width fields).
  - A medium nested struct (~100 fields, two levels of nesting).
  - A large array-heavy schema (1 MB encoded).
- Numbers are recorded to `docs/benchmarks/` per release. Regressions
  greater than 5% on any case fail CI.

## Notes

- "Equivalent optimization flags" means the same `-O` level, the same
  `-march`, and the same LTO setting. We do not give the C++ runtime a
  deliberate handicap.
- This is throughput, not latency, but a per-operation latency target
  may be added later (would be a new REQ-PF).
