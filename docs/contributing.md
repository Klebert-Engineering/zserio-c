# Contributing

For changes to `zserio-c` itself: the parser, generator, runtime,
or the supporting tooling.

If you're integrating zserio-c into your own build, you don't
need this page — start with [Getting started](getting-started.md).

## What this is

`zserio-c` is the C99 / ISO 26262 / ASIL-B port of the
[upstream zserio framework](https://github.com/ndsev/zserio).

The upstream pin is **v2.18.1** (`third_party/zserio/`). The C
output is byte-equal against the upstream cpp generator on all
182 cross-validation harness schemas, in both directions.

## Building

```sh
git clone https://github.com/Klebert-Engineering/zserio-c.git
cd zserio-c
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Build artefacts you'll care about:

- `build/parser/zserio_c_gen` — the C-native generator CLI
- `build/runtime/libzserio_c_runtime.a` — runtime sources compiled
- `build/tests/...` — runtime + parser unit tests

The build needs cmake ≥ 3.16, ninja (or GNU make), and a C99
toolchain. Python 3 is needed only for the xvalidation harness.

## Repository layout

```
parser/           C-native parser + generator (build target: zserio_c_gen)
  src/            lex.c, parse.c, sema.c, emit.c, expr.c
  cli/            command-line front-end
  include/        parser-private headers

runtime/          C99 runtime library
  include/zserio/ public headers (status, allocator, bitstream, ...)
  src/            implementations

generator/        Java extension that drives upstream zserio for
                  cpp-side reference output during xvalidation

tests/
  xvalidation/    bidirectional cpp ↔ C wire-format harness
    harnesses/    one directory per schema (182 total)
    run_xvalidation.sh
  runtime/        runtime unit tests (Unity)
  parser/         parser unit tests (Unity)
  benchmarks/     per-commit performance gate (REQ-PF-001)
  fuzz/           libFuzzer decode-path targets (REQ-FS-006)
  conformance/    upstream error-code mapping table

conformance/
  ndslive/        50 KiB NDS Live tile round-trip test
                  (1823 generated files, 86 .zs schemas)

docs/
  benchmarks/     RESULTS.md from the per-commit perf job
  safety-case/    hazard analysis, FMEA, requirements traceability
  user-guide/     end-user documentation
```

## Tests

There are five test gates, in roughly increasing cost:

### 1. Unit tests

```sh
cmake --build build --target test
ctest --test-dir build --output-on-failure
```

C99 unit tests for the runtime (bitstream, varint, arena, …) and
the parser (lex, parse, sema, emit). Targets gcc + clang with
strict warnings; under 30 s on a developer laptop.

### 2. Cross-validation (xvalidation)

The flagship correctness gate. 182 schemas covering every
upstream zserio language feature. For each schema:

- cpp_writes → c_reads → byte-equal
- c_writes → cpp_reads → byte-equal
- c_writes → c_reads (round-trip) → byte-equal

```sh
./tests/xvalidation/run_xvalidation.sh                      # plain
XVAL_SAN=1 ./tests/xvalidation/run_xvalidation.sh           # under ASan + UBSan
```

The harness needs the upstream zserio Java tool to produce the
cpp reference. The CI runner caches that step.

### 3. NDS Live conformance

A real 50 KiB NDS Live tile is decoded and re-encoded byte-equal.
The schema corpus is 86 `.zs` files producing 1823 generated
`.c` / `.h` files.

```sh
./conformance/ndslive/run_c_native.sh
```

This gate catches: cross-package emit paths, template
instantiation depth, identifier-length truncation, the
varsize-large path, packed compound arrays at scale.

### 4. Performance

Per-commit perf gate runs the benchmark workloads under
`tests/benchmarks/workloads/` against a per-host baseline
(`docs/benchmarks/perf.baseline.<host>.json`). A zserio-c
regression ≥ 15% fails the gate.

```sh
RUNS=5 INNER_ITERS=200000 bash tests/benchmarks/run_benchmark.sh > perf.json
python3 tools/check_perf_regression.py perf.json
```

Current numbers in [`docs/benchmarks/RESULTS.md`](benchmarks/RESULTS.md).

### 5. Coverage

```sh
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
lcov --capture --directory build/coverage --output-file coverage.info \
     --rc lcov_branch_coverage=1 --ignore-errors mismatch,unused,gcov
lcov --extract coverage.info '*/runtime/*' \
     --output-file coverage-runtime.info --rc lcov_branch_coverage=1
python3 tools/check_coverage.py coverage-runtime.info
```

Thresholds: line ≥ 99%, branch ≥ 85%, function 100%. Current
state: 99.89% / 88.03% / 100%.

## Code style

The runtime and generator are written to **MISRA C:2012** with the
deviations enumerated in [`docs/misra-deviations/`](misra-deviations/).
Key conventions:

- `snake_case` for identifiers, files, and directories
- One `typedef` per logical type; no anonymous structs in public
  headers
- Public functions return `zserio_status_t`; output is via pointer
  parameters
- No `goto` except for the in-function unified-cleanup pattern
- No dynamic format strings (`printf("%s", ...)` is fine,
  `printf(s, ...)` is not)
- No implicit heap allocation in the runtime; all allocation
  flows through the `zserio_allocator_t` vtable

Run a sanity check before sending a PR:

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build  # -Werror enabled
./tests/xvalidation/run_xvalidation.sh
```

## Adding a feature

Most schema-language coverage gaps go through the same loop:

1. **Reproduce upstream** — write the smallest schema that
   exercises the feature; add it as a harness under
   `tests/xvalidation/harnesses/<name>/`.
2. **Drive the cpp side** — `tests/xvalidation/harnesses/<name>/<name>.zs`
   plus a `cpp_driver.cpp` produces reference bytes via the upstream
   generator.
3. **Drive the c side** — add a `c_driver.c` that populates the
   same values via the C generator's output.
4. **Run** — `./tests/xvalidation/run_xvalidation.sh <name>` will
   show you exactly where the byte streams diverge.
5. **Fix in `parser/src/`** — usually in `emit.c` (codegen),
   sometimes in `sema.c` (typing / mangling).
6. **Auto-roundtrip** — once xval passes, run the bidirectional
   harness to catch read-side regressions:
   `XVAL_SAN=1 ./tests/xvalidation/run_xvalidation.sh`.

The 182 existing harnesses are the canonical examples of how to
shape a new one.

## Safety case

Changes that touch the runtime or the public API may require
updates to the safety case. The chain to check:

- [`docs/safety-case/hazard-analysis.md`](safety-case/hazard-analysis.md) — known hazards (H-01 .. H-N)
- [`docs/safety-case/functional-safety-concept.md`](safety-case/functional-safety-concept.md) — safety goals and failure-mode analysis
- [`requirements/`](../requirements/) — FS / SR / DR requirement IDs

If your change introduces a new failure mode, update the hazard
analysis and reference it from the PR description.

## Commits and PRs

- Short, imperative commit messages (`emit: align packed arrays
  on byte boundary`).
- One concern per commit. Cross-cutting refactors get their own
  PR.
- PRs link to the requirement ID(s) being touched if any.
- CI must pass — there's no override for red CI.

## Where to ask

Open an issue at https://github.com/Klebert-Engineering/zserio-c/issues.
Prefer reproducers as `tests/xvalidation/harnesses/<name>/` shells
with a minimal `.zs` + `c_driver.c`.
