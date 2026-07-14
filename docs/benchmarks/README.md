# Benchmarks — `zserio-c`

This directory holds the size and performance baselines that the
CI `size` and `perf` jobs gate against.

## Files

| File                    | What it is                                                                   |
| ----------------------- | ---------------------------------------------------------------------------- |
| `sizes.baseline.json`   | Per-section size baseline (`.text`, `.rodata`, `.data`, `.bss`) for the runtime static archive. Gated by `tools/check_size_regression.py` (REQ-PF-002). |
| `perf.baseline.json`    | Per-workload throughput baseline (encode + decode ns/op) for `zserio-c` and upstream cpp. Gated (advisory) by `tools/check_perf_regression.py` (REQ-PF-001). |

## Performance benchmark

The harness lives at `tests/benchmarks/run_benchmark.sh`. For each
workload under `tests/benchmarks/workloads/<name>/`:

1. Generate C + cpp code from `<name>.zs` (both runtimes from the
   same upstream toolchain at the pinned tag).
2. Compile a `c_driver.c` against `libzserio_c_runtime.a` and a
   `cpp_driver.cpp` against `libZserioCppRuntime.a`. Both at `-O2`.
3. Each driver does `INNER_ITERS` encode-then-decode iterations on
   a deterministic value and prints one line:
   `encode_ns_per_op=<x> decode_ns_per_op=<y> bytes=<z>`.
4. The harness runs each driver `RUNS` times (default 5) and takes
   the median per metric to suppress noise.
5. Aggregated output is a JSON document with per-workload numbers
   plus a `ratio_cpp_to_c` field (> 1.0 means we are faster).

Run locally:

```
bash tests/benchmarks/run_benchmark.sh > /tmp/perf.json
python3 tools/check_perf_regression.py /tmp/perf.json
```

To accept new numbers as the baseline:

```
bash tests/benchmarks/run_benchmark.sh > docs/benchmarks/perf.baseline.json
```

## Current workloads

| Workload | Schema | Status |
| --- | --- | --- |
| `small_flat` | 10 fixed-width fields covering the full int set + bool + bit:4 (~31 B encoded) | landed |
| `medium_nested` | nested structs across two levels | landed |
| `large_array` | array-heavy bulk-throughput schema | landed |
| `apollo`, `addressbook` (+`_align`, `_packed`, `_align_arena`), `carsales` (+`_align`, `_packed`), `simpletrace` (+`_packed`, `_packed_arena`) | corpus-derived workloads bootstrapped from the upstream benchmark datasets (`third_party/zserio-benchmarks-datasets`) | landed |
| `ndslive_tile` | 50 KiB production NDS.Live payload | private overlay only |

The `ndslive_tile` workload ships with the private NDS.Live
conformance overlay (a companion repository that checks out on top
of this tree; NDS Association membership required). When the
overlay is absent the harness simply does not see the workload;
when it is present without its assets extracted,
`run_benchmark.sh` skips it cleanly.

The measured numbers per host live in
[`RESULTS.md`](RESULTS.md); the per-host gate baselines in
`perf.baseline.<host>.json`.

## Gate semantics

The `perf` CI job is **advisory** in its initial form.
Public ubuntu-latest CI runners have significant per-run noise on
throughput numbers, and we do not yet have a CI-measured baseline.
The job posts the full benchmark report as a commit comment so the
numbers are visible per commit, but does not fail the build on a
regression.

The plan is to:

1. Land the harness (this commit).
2. Observe 2–3 green CI runs to characterize the noise floor on the
   runner.
3. Replace `perf.baseline.json` with the median of those runs.
4. Drop the advisory `|| true` in `.github/workflows/ci.yml` and
   tune `REGRESSION_PCT_FAIL` in `tools/check_perf_regression.py`
   to a level that gates real regressions without flapping on
   noise.

The cpp ratio (`ratio_cpp_to_c`) is reported for visibility but is
not itself a gate; closing the ratio toward 1.0 is the substantive
optimization work that this measurement framework exists
to track. The bit-cache + mask-table read pattern documented in
`runtime/src/bitstream.c:78-80` is the first planned step.
