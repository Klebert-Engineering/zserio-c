---
id: REQ-PF-001
title: Throughput at least matching upstream C++ runtime
type: performance
status: accepted
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

- A benchmark harness (`tests/benchmarks/`) measures
  read and write throughput across a corpus of upstream-derived
  workloads:
  - Small flat struct (`small_flat`, 10 primitive fields, ~31 B).
  - Medium nested struct (`medium_nested`, ~100 fields, 2 levels).
  - Large array (`large_array`, 1 MB primitive-element auto-array).
  - Upstream zserio benchmarks: `addressbook`, `carsales`,
    `simpletrace` and their `_align` / `_packed` variants.
  - Showcase: `addressbook_align_arena` exercises the bump-arena
    deployment shape (matched against a pmr cpp_driver).
- The harness lives in `tests/benchmarks/run_benchmark.sh` and runs
  each workload `RUNS=5` times per host, taking the median ns/op.
- Numbers are recorded per host at
  `docs/benchmarks/perf.baseline.<host>.json`. The active hosts are
  `primary` (AMD x86_64) and `pi4` (Raspberry Pi 4 Cortex-A72).
- `tools/check_perf_regression.py` is the gate. It compares the
  current run against the same host's baseline file and fails on a
  zserio-c metric regression > 15% (`REGRESSION_PCT_FAIL`). It also
  emits an advisory `↓ rebaseline?` hint at ≥ 30% improvement so
  stale baselines get noticed. The 15% threshold is loose by design
  to absorb runner-noise; tightening per host is a follow-up once
  the longer-term noise floor is characterized.

## Notes

- "Equivalent optimization flags" means the same `-O` level, the same
  `-march`, and the same LTO setting. We do not give the C++ runtime a
  deliberate handicap.
- This is throughput, not latency, but a per-operation latency target
  may be added later (would be a new REQ-PF).
