# zserio-c

C99 port of the [zserio](https://github.com/ndsev/zserio) schema IDL, designed
from day one for use in automotive functional-safety systems (ISO 26262,
target ASIL-B, architectural headroom toward ASIL-D).

## Status

**Phase 5 substantively complete · Phase 6 active.** Every CI gate green on
`main`. Wire compatibility with upstream `ndsev/zserio v2.18.1` is gated at
182 schemas × 5 seeds per commit. The ISO 26262 safety case is drafted end
to end under [`docs/safety-case/`](docs/safety-case/), pending independent
review for `draft → accepted`. Phase 6 is now in measurement-first mode:
the `perf` CI job runs the benchmark harness on every commit (advisory)
while the optimisation work that closes the cpp ratio is staged.

| Phase | Status | Marker |
| ----- | ------ | ------ |
| 0 — Bootstrap, upstream analysis | ✅ done | repo + submodule + six upstream-analysis deep-dives |
| 1 — Runtime MVP | ✅ done | 11 source files, 9 unit-test files |
| 2 — Java generator extension | ✅ done | `generator/zserio_c.jar` builds end to end |
| 3 — Full schema-language coverage | ✅ done | 391/391 grammar acceptance on upstream test corpus |
| 4 — Conformance against upstream test corpus | ✅ done | `xvalidation` CI gate: 182 schemas × 5 seeds byte-equal vs cpp |
| 5 — Safety case maturation | 🟢 substantively complete | 14 work products in `docs/safety-case/`, all `draft`; one Open item remaining (decode-path fuzzing). G3 — generator-output MISRA gate — closed `c13bd41`; G4 — transitive stack budget — closed via gcc `-fcallgraph-info` longest-path traversal (current max 512 B / 1024 B ceiling) |
| 6 — Performance benchmarks vs C++ runtime | 🟢 measurement live (advisory) | `perf` CI job: harness + cpp-ratio reporting; optimisation work to close the ratio is open |
| 7 — C-native parser replaces Java front-end | ⬜ open | |

See [`docs/architecture.md`](docs/architecture.md) for the design and
[`docs/safety-case/safety-case-report.md`](docs/safety-case/safety-case-report.md)
for the assembled safety argument.

## What's in CI

Every commit to `main` runs the following gates. All currently pass:

| Gate | What it proves | Tooling |
| ---- | -------------- | ------- |
| Build and test (gcc) | Runtime compiles and 11 Unity test suites pass | gcc + ctest |
| Build and test (clang) | Independent toolchain catches gcc-specific miscompilation | clang + ctest |
| Wire-format cross-validation (REQ-FN-001) | 182 schemas × 5 seeds × byte-equal vs upstream cpp | `tests/xvalidation/run_xvalidation.sh` |
| MISRA C:2012 (strict) + forbidden-symbol grep | Zero MISRA findings; no banned-libc calls | `tools/run_misra.sh --strict` |
| Coverage (line ≥ 99% / branch ≥ 85% / function = 100%) | Snapshot: 99.89% / 88.03% / 100% | gcov + lcov + `tools/check_coverage.py` |
| Size regression (REQ-PF-002) | ≤ 5% growth on any of `.text` / `.rodata` / `.data` / `.bss` | `tools/measure_size.py` + `tools/check_size_regression.py` |
| Stack-budget analysis (REQ-FS-003, hazard H-09) | Every function has a static, ≤ 256 B frame | `tools/measure_stack_usage.py --strict` |
| Requirements traceability | Every accepted in-phase requirement has a `Verifies:` marker | `tools/check_traceability.py` |
| Phase 2 generator | The generator jar builds and loads end to end | ant |
| Performance benchmark (REQ-PF-001, advisory) | Median-of-5 encode/decode ns/op for each benchmark workload, side-by-side vs upstream cpp | `tests/benchmarks/run_benchmark.sh` |
| Test-data discovery (advisory) | Reports schemas in the upstream corpus not yet in xvalidation | bash |

## Quick start

```bash
# Configure + build runtime + tests
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

# Generate code from a schema (requires Java 17 + ant)
tools/build_zserio_core_jar.sh           # upstream core jar (one-shot)
tools/build_generator_jar.sh              # our generator extension
java -cp build-jars/zserio_core.jar:generator/build/jar/zserio_c.jar:build-jars/zserio.jar \
     zserio.tools.ZserioTool \
     -src path/to/your/schemas your_schema.zs \
     -c path/to/output

# Cross-validate against upstream cpp (requires both submodules)
git submodule update --init --recursive
bash tests/xvalidation/run_xvalidation.sh        # 182 schemas × 5 seeds

# Run the local stack-budget gate
python3 tools/measure_stack_usage.py --strict

# Run the local size gate
python3 tools/measure_size.py > /tmp/sizes.json
python3 tools/check_size_regression.py /tmp/sizes.json
```

## Project goals

The four pillars are CI-gated, not aspirational:

- **Pure C99 generated code** — `-std=c99 -pedantic -Werror` plus the strict
  warning set in `CMakeLists.txt`. No compiler extensions, no GCC-isms.
- **No implicit heap** — `runtime/src/*.c` and generated code never call
  `malloc`/`calloc`/`realloc`/`free` directly. All allocation flows through
  the caller-supplied `zserio_allocator_t` vtable. Enforced by the
  forbidden-symbol grep in `tools/run_misra.sh`. The default backend wraps
  libc malloc; safety builds plug a fixed pool.
- **MISRA C:2012 strict** with documented, justified deviations (six entries
  under [`docs/misra-deviations/`](docs/misra-deviations/)). No suppression
  without a sibling deviation file.
- **ISO 26262 alignment from commit one** — the safety case is grown
  alongside the code, not retrofitted. See
  [`docs/safety-case/`](docs/safety-case/).
- **Coverage gate** — line ≥ 99%, branch ≥ 85%, function = 100% on
  `runtime/src/*`. Currently 99.89% / 88.03% / 100%.
- **Wire-compatible** with upstream `ndsev/zserio v2.18.1` — gated for 182
  schemas × 5 seeds per commit, byte-equal in both directions.
- **Bounded stack** — every function has a static, ≤ 256 B frame (current
  observed max 144 B); combined with no-recursion (REQ-FS-003) this bounds
  the worst-case call-chain total.

## Why a port

The upstream C++ generator and runtime are not viable for automotive
functional-safety production:

- STL containers grow under the hood through their allocator template
  parameter, but in practice an allocator override is impractical across an
  entire codec — so the default heap is used.
- Errors propagate via exceptions; ASIL builds typically forbid them.
- RTTI is present in several places. The schema parser pulls in
  `<typeinfo>`.

`zserio-c` removes these by construction: status codes instead of
exceptions, an explicit allocator vtable instead of templates, no
runtime reflection in generated code. See
[`docs/upstream-analysis/05-asil-pain-points.md`](docs/upstream-analysis/05-asil-pain-points.md)
for the detailed enumeration that motivated each design choice.

## Repository layout

```
runtime/             libzserio_c_runtime — bitstream, varint, primitives,
                     delta, allocator, status
generator/           Java generator extension that plugs into upstream's
                     ZserioTool and emits C against the runtime
third_party/zserio/  upstream submodule, pinned at v2.18.1
third_party/zserio-test-data/  upstream test-corpus submodule

docs/architecture.md           — design intent (load-bearing)
docs/coding-rules.md           — code style + MISRA framing
docs/safety-case.md            — entry point for the safety case
docs/safety-case/              — 14 ISO 26262 work products
docs/misra-deviations/         — D001..D006 with cppcheck-suppressions cross-refs
docs/upstream-analysis/        — six deep-reads of upstream zserio
docs/benchmarks/               — size baseline (perf baseline coming in Phase 6)

requirements/                  — REQ-FN-* (functional), REQ-FS-* (safety),
                                 REQ-PF-* (performance), one .md per requirement

tests/runtime/                 — Unity unit tests, 11 suites
tests/xvalidation/             — 182-schema byte-equal cross-validation harness
tests/benchmarks/              — REQ-PF-001 throughput harness (median-of-5
                                 encode/decode vs upstream cpp)
conformance/ndslive/           — NDS Live SmartLayerTile end-to-end fixture

tools/                         — measure_size, check_size_regression,
                                 measure_stack_usage, check_coverage,
                                 check_perf_regression, check_traceability,
                                 run_misra, build_zserio_core_jar,
                                 build_generator_jar, build_zserio_bundle_jar
.github/workflows/ci.yml       — the 11 gates listed above
```

## Documentation entry points

For the design: [`docs/architecture.md`](docs/architecture.md).

For the safety story:
[`docs/safety-case/safety-case-report.md`](docs/safety-case/safety-case-report.md) — single-document overview of the assembled argument, links into every supporting work product.

For coding rules and MISRA framing:
[`docs/coding-rules.md`](docs/coding-rules.md).

For the upstream-zserio analysis that motivated v1 decisions:
[`docs/upstream-analysis/`](docs/upstream-analysis/).

## License

MIT — see [`LICENSE`](LICENSE). Upstream zserio under `third_party/zserio/`
remains under its own BSD-3-Clause license; the upstream test data under
`third_party/zserio-test-data/` retains its own terms. Neither submodule's
content is redistributed in the shipping binary.
