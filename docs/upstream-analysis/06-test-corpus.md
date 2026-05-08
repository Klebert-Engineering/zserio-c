# 06 · Test corpus — structure and conformance reuse plan

**Purpose.** The last of the upstream-analysis deep-dives. Specifies
how the upstream test corpus is laid out, how its tests are run, and a
concrete plan for consuming the 26 in-scope language categories
(`01-schema-language.md` §4) into our Phase 4 conformance gate.

**Authoritative sources** (at pinned tag `v2.18.1`):
- `third_party/zserio/test/extensions/language/<category>/` — per-category
  test definitions (CMake + Ant + per-language test sources).
- `third_party/zserio/test/data/` — **separate submodule** holding
  the actual `.zs` schemas. Pulled from `https://github.com/ndsev/zserio-test-data.git`.
- `third_party/zserio/test/extensions/CMakeLists.txt` — top-level test
  CMake driver.

## 1. Where the corpus actually lives — the submodule gotcha

`test/data/` in the upstream tree is empty in a plain submodule clone.
It is itself a Git submodule pointing at `ndsev/zserio-test-data`. From
upstream's `.gitmodules`:

```
[submodule "test/data"]
    path = test/data
    url = https://github.com/ndsev/zserio-test-data.git
```

This means our Phase 4 work needs **recursive** submodule init from
the top of our repo:

```
git submodule update --init --recursive
```

We should document this in our README and in any CI workflow that
needs the corpus. (The Phase 0 `cmake-configure` workflow does NOT
need it — only Phase 4 conformance does.) The simpler option is to
init only the inner submodule when needed:

```
git -C third_party/zserio submodule update --init test/data
```

When the corpus pull happens, `test/data/` populates with **666 `.zs`
schema files** organized by category and feature. We have already done
this locally; the parent repo's tracked commit pointer for
`third_party/zserio` is unchanged, so the inner-submodule init is a
working-tree concern only.

## 2. Per-category corpus structure

Each category under `test/data/language/<category>/` is laid out as:

```
test/data/language/<category>/
└── zs/
    ├── <category>.zs                    # umbrella package importing all sub-schemas
    └── <category>/
        ├── <feature_a>.zs               # one schema file per tested feature
        ├── <feature_b>.zs
        └── ...
```

The corresponding test sources under `test/extensions/language/<category>/`:

```
test/extensions/language/<category>/
├── CMakeLists.txt                       # cpp build descriptor (calls zserio_generate_cpp)
├── build.xml                            # Ant build descriptor (java/python/cpp drivers)
├── ClangTidySuppressions.txt            # per-category clang-tidy carve-outs
├── spotbugs_filter.xml                  # per-category SpotBugs (Java) carve-outs
├── cpp/
│   ├── <Feature_a>Test.cpp              # GoogleTest TEST_F file per feature
│   └── <Feature_b>Test.cpp
├── java/<category>/
│   ├── <Feature_a>Test.java
│   └── ...
└── python/
    ├── <Feature_a>Test.py
    ├── <Feature_b>Test.py
    └── <Category>.py                    # Python package init / harness
```

The per-language test files all target the same set of `<feature>.zs`
schemas, demonstrating language parity. For us, only the **C generator
output** matters — we don't need to match upstream's Java or Python
test files.

## 3. The cpp test pattern (the one we mirror)

Each cpp test file:

1. `#include "gtest/gtest.h"`
2. `#include "<category>/<feature>/<TypeName>.h"` — the **generated**
   header from running the upstream cpp generator on the `.zs` file.
3. `#include "zserio/BitStreamReader.h"` and `BitStreamWriter.h` —
   upstream runtime entry points.
4. Defines a `TEST_F` fixture that:
   - Writes a hand-crafted byte sequence to a `BitStreamWriter` using
     low-level `writer.writeBits(value, bitWidth)` calls.
   - Constructs the generated type by reading from a `BitStreamReader`
     positioned at the start of the buffer.
   - Asserts that getter values match expected.
   - Reverse: encodes a constructed instance, asserts byte equality
     with the hand-crafted bytes.
   - Round-trips with `eq`/`ne`/`hash`/`<` operators.

Sample (`structure_types/cpp/SimpleStructureTest.cpp`):

```cpp
TEST_F(SimpleStructureTest, bitStreamReaderConstructor) {
    const uint8_t numberA = 0x07, numberB = 0xFF, numberC = 0x7F;
    zserio::BitStreamWriter writer(bitBuffer);
    writeSimpleStructureToByteArray(writer, numberA, numberB, numberC);

    zserio::BitStreamReader reader(writer.getWriteBuffer(),
                                   writer.getBitPosition(),
                                   zserio::BitsTag());
    SimpleStructure simpleStructure(reader);
    ASSERT_EQ(numberA, simpleStructure.getNumberA());
    ASSERT_EQ(numberB, simpleStructure.getNumberB());
    ASSERT_EQ(numberC, simpleStructure.getNumberC());
}
```

The schema for that test (`test/data/language/structure_types/zs/structure_types/simple_structure.zs`):

```
package structure_types.simple_structure;

struct SimpleStructure
{
    bit:3   numberA;
    uint8   numberB;
    bit:7   numberC;
};
```

Two key properties we exploit:

- The expected wire format is encoded **explicitly** in the test
  (`writer.writeBits(...)` calls) — this is our source of truth for
  what bytes `SimpleStructure` should produce, independent of the
  generator that generated it.
- Each schema is small and self-contained — easy to drive
  programmatically.

## 4. Per-category numbers, in-scope only

Counted directly from `test/data/language/*/zs/**/*.zs` and
`test/extensions/language/*/cpp/*.cpp`:

| #  | Category               | Schemas | Cpp tests |
|----|------------------------|--------:|----------:|
| 1  | `alignment`            |       4 |     (sm)  |
| 2  | `array_types`          |      31 |    (lg)   |
| 3  | `bitmask_types`        |       9 |     (md)  |
| 4  | `builtin_types`        |       5 |       4   |
| 5  | `choice_types`         |      23 |    (lg)   |
| 6  | `comments`             |      36 |     (md)  |
| 7  | `constants`            |       1 |       1   |
| 8  | `constraints`          |       9 |     (md)  |
| 9  | `default_values`       |       5 |     (sm)  |
| 10 | `enumeration_types`    |      11 |     (md)  |
| 11 | `explicit_parameters`  |       9 |     (md)  |
| 12 | `expressions`          |      30 |     (md)  |
| 13 | `extended_members`     |      13 |     (md)  |
| 14 | `functions`            |      15 |     (md)  |
| 15 | `identifiers`          |       3 |     (sm)  |
| 16 | `indexed_offsets`      |      19 |     (md)  |
| 17 | `literals`             |       1 |     (sm)  |
| 18 | `member_access`        |       2 |     (sm)  |
| 19 | `offsets`              |      17 |     (md)  |
| 20 | `optional_members`     |      10 |     (md)  |
| 21 | `packages`             |      24 |     (md)  |
| 22 | `parameterized_types`  |      19 |     (md)  |
| 23 | `structure_types`      |       9 |       7   |
| 24 | `subtypes`             |       8 |     (sm)  |
| 25 | `templates`            |      63 |    (lg)   |
| 26 | `union_types`          |      10 |     (md)  |

**In-scope totals: ~386 schemas, ~252 cpp test files.**

(`(sm)`/`(md)`/`(lg)` denote rough size buckets where exact per-category
cpp file counts weren't enumerated. They are not load-bearing for the
plan; the schema count is.)

The `templates` category at 63 schemas is the largest. It's the
broadest stress on the generator's template-instantiation logic.

## 5. Conformance reuse plan

### 5.1 The strategic choice

Two strategies for using this corpus to gate our conformance:

**Strategy A — Port the cpp test files to C**: take each `.cpp` test,
mechanically translate the `TEST_F` to a Unity test (or our own minimal
equivalent), translate `BitStreamWriter::writeBits` calls to our
`zserio_write_bits` calls, and use our generated headers instead of
upstream's. Maintain ~252 ported test files in our tree.

Pros: tests document our generated API directly; failures point at
specific assertions.
Cons: ~252 files of mechanical port work; every upstream test change
needs a re-port; bug-for-bug compatibility with upstream's test
expectations is on us.

**Strategy B — Cross-validation against upstream (Recommended)**:
treat upstream's already-built generator + runtime as a **reference
oracle**. For every schema in `test/data/language/<in_scope_category>/zs/`,
generate random valid values, encode and decode through BOTH upstream
and zserio-c, byte-compare. The tests are written once (a generic
harness driven by schema metadata), not per category.

Pros: O(1) test code instead of O(252). Catches every wire-format
disagreement automatically; doesn't depend on what upstream's existing
tests happen to assert; covers schemas upstream doesn't have explicit
tests for. Stronger guarantee than the port approach.
Cons: requires a programmatic value generator per schema (random walk
of the AST); requires linking upstream's runtime as a test dependency.

We pick **Strategy B**. The per-schema cost is constant; the per-category
cost is zero.

### 5.2 What the conformance harness looks like

```
tests/conformance/
├── CMakeLists.txt
├── harness/
│   ├── value_generator.{c,h}            # seeded PRNG -> random valid value per schema type
│   ├── upstream_oracle.{cpp}            # links upstream runtime; encode/decode reference
│   ├── round_trip.{c,h}                 # the four-way comparison (see below)
│   └── runner.c                         # iterates the corpus, runs round_trip per schema
├── schemas.csv                          # generated: schema → top-level type → generator args
└── README.md
```

For each schema, the runner performs:

1. **Build phase** (CMake-time): invoke our `zserio_c` generator AND
   upstream's `zserio_cpp` generator on the same `.zs` file. Two
   parallel generated trees; both link the runner.
2. **Test phase** (per schema, per N seeded values):
   - `value_generator(seed)` produces a random valid in-memory
     instance: `value_c` (in our generated C struct form) and
     `value_cpp` (in upstream's C++ class form), with identical field
     values.
   - Encode `value_c` through our runtime → `bytes_c`.
   - Encode `value_cpp` through upstream runtime → `bytes_cpp`.
   - **Assertion 1**: `bytes_c == bytes_cpp` (byte-exact).
   - Decode `bytes_cpp` through our runtime → `decoded_c`.
   - Decode `bytes_c` through upstream runtime → `decoded_cpp`.
   - **Assertion 2**: `decoded_c == value_c` (round-trip on our side).
   - **Assertion 3**: `decoded_cpp == value_cpp` (round-trip on
     upstream side, i.e. our bytes parse correctly there).

Three assertions per schema per seed. With ~386 schemas × N = 100
seeded values × 3 assertions = ~115 000 assertions per CI run. CI
budget on the order of a few minutes; manageable.

### 5.3 Schema discovery

A small Python script (or generator-side hook) scans
`third_party/zserio/test/data/language/<in_scope_category>/zs/**/*.zs`
and emits the `schemas.csv` index used by the runner. Each row records:
`<schema_path>, <top_level_package>, <top_level_type>, <generator_extra_args>`.

Out-of-scope categories (`pubsub_types`, `service_types`, `sql_*`,
`rules`) are excluded by file-path filter.

### 5.4 The value-generator strategy

For each schema's top-level type, the runner needs a way to produce a
random valid instance. Three approaches in increasing order of
effort:

- **Generator-emitted random helper**: our generator emits a
  `<type>_random(seed_t *seed)` function alongside the encode/decode
  helpers. Valid by construction (it is generated from the schema
  too), supports parameters, constraints, optionals, `extend`. Cost:
  one extra FreeMarker template family in our generator.
- **AST-driven walker**: the harness reads the `.zs` directly via the
  upstream Java tool (with a custom extension that emits a JSON dump
  of each top-level type's structure), walks that JSON, and produces
  a random value programmatically. Cost: one Java extension + one
  walker in the harness.
- **Reflection through upstream's `IReflectable`**: build a value via
  upstream's reflection API. Cheapest in lines of code — but
  reflection is out of scope for our generator (REQ-FS-008), so we'd
  rely on it only on the upstream side. Risky because it couples the
  oracle's behavior to upstream's reflection bugs.

**Recommended**: option 1 (generator-emitted). It is the cleanest
self-test ("the generator's output validates itself") and gives us a
useful side-product: a fuzzing utility for downstream consumers. The
cost is one extra template family, gated behind a `-cWithRandomCode`
generator flag (off by default in production builds, on in
conformance and fuzz builds).

### 5.5 Coverage, not just pass/fail

Each schema run produces a **coverage delta** — which `.c`/`.h` lines
in the runtime were exercised. The conformance suite therefore double-
serves as the runtime coverage gate for REQ-FS:001..008's verification
clauses (most of which name `verification: analysis` plus tests).

Phase 1 enforces 100% line + branch coverage on the runtime via gcov.
Phase 4 conformance is one of the major coverage drivers, alongside
the dedicated unit tests.

### 5.6 Negative tests

The corpus also contains `test/extensions/language/<category>/` cases
that **must fail** (decode of a value violating a constraint, decode
of an enum value out of range, etc.). Upstream marks these in
`test/data/<category>/zs/...` schemas with deliberately wrong values
in their cpp tests.

Our harness runs negative tests via a per-schema "negative seed"
mechanism: the value generator can be asked to produce a deliberately
malformed byte sequence and assert that both runtimes return error
status. For our runtime, "error status" means a specific
`ZSERIO_ERR_*` value; we assert the *category* matches upstream's
exception type (e.g. upstream `ConstraintException` ↔
`ZSERIO_ERR_CONSTRAINT_VIOLATED`).

This mapping is the same one parked in `05-asil-pain-points.md` §10:
the 45 upstream throw sites map to specific `ZSERIO_ERR_*` codes. The
mapping table belongs in `tests/conformance/error_mapping.csv` and is
exercised by the negative-test harness.

## 6. CI architecture

```yaml
# .github/workflows/conformance.yml (added at Phase 4)

name: Conformance
on:
  pull_request:
  push: { branches: [main] }
  schedule:
    - cron: '0 6 * * *'   # nightly full run

jobs:
  conformance:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive       # picks up test/data inside third_party/zserio
      - uses: actions/setup-java@v4
        with: { java-version: '17' }
      - name: Build upstream zserio jar
        run: |
          cd third_party/zserio
          ant -f build.xml core.jar cpp.jar
      - name: Build our generator jar
        run: ./gradlew :generator:jar
      - name: Configure
        run: cmake -B build -S . -DZSERIO_C_BUILD_CONFORMANCE=ON
      - name: Build
        run: cmake --build build --target conformance
      - name: Run
        run: ctest --test-dir build --output-on-failure
        env:
          CONFORMANCE_SEEDS: ${{ github.event_name == 'schedule' && '1000' || '100' }}
```

Two key behaviors:

- **Per-PR**: 100 seeds per schema. Few-minute run. Catches obvious
  regressions.
- **Nightly**: 1000 seeds per schema. Catches rare-state divergences.
  Failures triage to a follow-up issue rather than blocking.

Both runs gate on the same assertions; only the seed count differs.

## 7. What we add to our repo for this

To not surprise anyone in Phase 4, here is the concrete additive
footprint over our current scaffold:

- `tests/conformance/CMakeLists.txt` — declares the conformance
  executable, links upstream runtime + our runtime + the harness.
- `tests/conformance/harness/*.{c,h}` — the harness sources (~500
  lines C, ~200 lines C++ for the upstream-oracle bridge).
- `tests/conformance/error_mapping.csv` — `ZSERIO_ERR_*` ↔ upstream
  exception type.
- `tools/build_upstream_jar.sh` — bootstraps the upstream
  `zserio.jar` from the submodule. Cached by CI.
- Generator option `-cWithRandomCode` and the corresponding
  `Random.inc.ftl` template (Phase 2 work item).
- `.github/workflows/conformance.yml` — added at Phase 4 start, not
  earlier (no point running the workflow with no implementation).

## 8. Open questions for Phase 4

1. **`comments` and `identifiers` categories in conformance scope?**
   These test the generator's name-mangling and doc-comment
   propagation. They have no wire-format impact, so the cross-
   validation harness as defined in §5.2 doesn't catch their
   regressions. Recommend: a separate "generator-output golden file"
   test for these two categories (compare emitted `.c`/`.h` against
   committed reference output for a stable schema set). Phase 2/3
   work item, not Phase 4.
2. **Floating-point round-trip equality**: `float16` quiet NaN and
   denormal handling can differ between hosts. Recommend: byte-compare
   the encoded form (always exact); compare decoded values
   bit-pattern-wise (memcmp on the bit representation), not via
   `==` (which is undefined for NaN).
3. **Random value generator coverage**: with N = 100 seeds per schema,
   are we likely to hit every constraint branch / every choice tag /
   every bitmask-flag combination? For schemas with deeply nested
   choices, no. We may need a **structured generator** (enumerate tags
   and constraint corner cases deterministically, then random-fill
   the leaves). Decide during Phase 4.
4. **Cost of building upstream's `zserio.jar` from source on every
   CI run**: ~minutes. Cacheable by GitHub Actions cache action keyed
   on the submodule SHA. Set up once.
5. **`templates` category at 63 schemas**: will every template
   instantiation make it into the schema list? Templates are resolved
   at parse time, so each `instantiate` produces one entry; that is
   already what the corpus has. No special handling needed.

## 9. Implications for the project tracker

- **Phase 2** (generator) acquires a sub-deliverable: `-cWithRandomCode`
  and the random-helper template family.
- **Phase 4** (conformance) is meaningfully larger than originally
  estimated — adds the harness build and the upstream-jar build to
  the CI critical path.
- **Phase 1** (runtime) gains a follow-up: the `error_mapping.csv`
  belongs alongside the `zserio_status_t` enum lockdown
  (parked in `05-asil-pain-points.md` §10).
- **Phase 0** (now) acquires one small action item: documenting the
  recursive submodule init in our README. Otherwise no scaffold
  changes needed.

## 10. Acceptance

- [x] Test corpus location identified, including the inner-submodule
      gotcha (§1).
- [x] Per-category structure and the cpp test pattern documented
      with citations (§2, §3).
- [x] Per-in-scope-category schema and test counts enumerated (§4).
- [x] Conformance reuse strategy chosen with rationale: cross-
      validation against upstream as oracle, not test-file porting
      (§5).
- [x] CI architecture sketched with two-tier seed budget (§6).
- [x] Concrete additive scaffold for Phase 4 listed (§7).
- [ ] Phase 4 open questions in §8 are resolved when Phase 4 begins —
      not now.
