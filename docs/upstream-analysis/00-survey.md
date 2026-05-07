# Upstream survey — `ndsev/zserio` at `v2.18.1`

This is the first document in `docs/upstream-analysis/`. Its job is to
give us a shared map of the upstream codebase before we start deep-diving
into specific topics. It is intentionally broad and shallow — every
section ends pointing at the deeper document where the topic gets a
proper treatment.

References in this file are to paths inside `third_party/zserio/`,
relative to that submodule's root, at the pinned tag `v2.18.1`.

## 1. Top-level layout

```
third_party/zserio/
├── compiler/
│   ├── core/              the zserio compiler — Java + ANTLR4
│   └── extensions/        code generators (cpp, java, python, doc, xml)
├── ant_task/              Ant integration for invoking the compiler
├── benchmarks/            addressbook, apollo, carsales, datasets, simpletrace
├── test/
│   ├── core/              compiler-level tests (errors, warnings, utilities)
│   ├── data/              shared schema data
│   └── extensions/        generator-level tests, organized by feature
├── doc/                   user-facing documentation (Markdown)
├── 3rdparty/              googletest, sqlite, java deps
├── cmake/                 CMake glue used by the C++ runtime build
├── mvn/                   Maven publishing config
├── scripts/               build helpers (bash)
└── build.xml              top-level Ant target
```

## 2. The compiler core (`compiler/core/`)

The thing we ultimately replace in Phase 7.

- ANTLR4 grammar in `compiler/core/antlr/`:
  - `ZserioLexer.g4`   — 160 lines
  - `ZserioParser.g4`  — 482 lines
  - `DocComment.g4`    — 133 lines
- Java sources in `compiler/core/src/`: **143 files**, organized into:
  - `zserio/antlr/` — ANTLR adapter glue
  - `zserio/ast/` — the AST node hierarchy (this is the SPI generators consume)
  - `zserio/extension/` — the extension SPI itself, plus shared helpers (`extension/common/`, `extension/common/sql/`)
  - `zserio/tools/` — CLI driver

Phase 7 scope budget: ~775 lines of grammar + ~143 Java files of AST and
semantic analysis. Tractable for a C99 hand-written recursive descent
parser plus tree-walking semantic passes, but not in the v1 path.

## 3. Code generators (`compiler/extensions/`)

Five generators, all Java extensions plugging into the same SPI.

| Extension  | Purpose                | Java sources | FreeMarker (.ftl) |
|------------|------------------------|-------------:|------------------:|
| `cpp/`     | C++11 generator        |           73 |                32 |
| `java/`    | Java generator         |    (similar) |                20 |
| `python/`  | Python generator       |    (similar) |                19 |
| `doc/`     | HTML/PDF doc generator |    (similar) |                28 |
| `xml/`     | XML schema dump        |    (similar) |                 0 |

Each follows the same shape:

```
compiler/extensions/<name>/
├── src/                   Java extension code (Ant-built)
├── freemarker/            .ftl templates the generator emits from
├── runtime/               per-language runtime library (separate build)
├── metainf/               JAR manifest hooks
├── test/                  extension-level Java tests
├── build.xml              Ant build descriptor
└── README.md              user-facing doc for this generator
```

**Our generator (`zserio-c`) is a sixth sibling.** It will live in
`generator/` in this repo (not `compiler/extensions/c/` upstream — we
are external to upstream), built independently, packaged as a JAR that
the upstream compiler loads via the same SPI everyone else uses.

→ Deep dive: [`03-plugin-spi.md`](03-plugin-spi.md).

## 4. C++ runtime — anatomy and visible ASIL footprint

`compiler/extensions/cpp/runtime/src/zserio/` contains **65 public headers
+ 13 `.cpp` source files** (excluding the `pmr/` subtree). This is the
codebase whose surface we are matching with a smaller, simpler runtime.

Subsystems visible from headers alone:

- **Wire-format core** — `BitStreamReader`, `BitStreamWriter`, `BitFieldUtil`,
  `BitPositionUtil`, `BitSizeOfCalculator`, `Array`, `ArrayTraits`,
  `BitBuffer`. This is the part we care most about and will mirror in
  C99.
- **Numeric / encoding helpers** — `FloatUtil`, `SizeConvertUtil`,
  `BuiltInOperators`, `VarSizeUtil`.
- **Compound-type machinery** — `OptionalHolder`, `AnyHolder`,
  `DeltaContext`, `Enums`.
- **Allocator support** (parallel `pmr/` tree, 18 files) — `MemoryResource`,
  `NewDeleteResource`, `PolymorphicAllocator`, plus per-container
  variants (`pmr/Vector.h`, `pmr/Map.h`, `pmr/Set.h`, `pmr/String.h`,
  `pmr/UniquePtr.h`, `pmr/HeapOptionalHolder.h`). **This is the closest
  upstream analog to our allocator-injection model and deserves a careful
  read before we lock the `zserio_allocator_t` ABI.**
- **Reflection / RTTI** — `IReflectable`, `ITypeInfo`, `Reflectable`,
  `ReflectableUtil`. Not in zserio-c v1 scope.
- **JSON I/O** — `JsonEncoder`, `JsonDecoder`, `JsonParser`, `JsonReader`,
  `JsonTokenizer`, `JsonWriter`. Not in zserio-c v1 scope.
- **Service / Pubsub / SQLite** — `IService`, `ServiceException`,
  `IPubsub`, `PubsubException`, `ISqliteDatabase`, `ISqliteDatabaseReader`,
  `SqliteException`. Not in zserio-c v1 scope.
- **Exceptions** — `CppRuntimeException`, `ConstraintException`,
  `PubsubException`, `ServiceException`, `SqliteException`. **Six
  exception types we replace with status codes.**
- **Walk / debug tooling** — `IWalkObserver`, `IWalkFilter`,
  `IValidationObserver`, `DebugStringUtil`. Likely not in v1.

→ Deep dive: [`04-cpp-runtime-internals.md`](04-cpp-runtime-internals.md).
→ Pain-points enumeration: [`05-asil-pain-points.md`](05-asil-pain-points.md).

## 5. Test corpus (`test/`)

Three top-level groups:

- `test/core/` — tests for the compiler itself (`errors/`, `warnings/`,
  `utils/`).
- `test/data/` — shared schema data used by both core and extension tests.
- `test/extensions/` — generator-level tests, organized by feature:
  - `arguments/` — generator CLI argument handling
  - `errors/` — error-path tests
  - **`language/` — 36 feature categories**: `alignment`, `array_types`,
    `bitmask_types`, `builtin_types`, `choice_types`, `comments`,
    `constants`, `constraints`, `default_values`, `enumeration_types`,
    `explicit_parameters`, `expressions`, `extended_members`, `functions`,
    `identifiers`, `indexed_offsets`, `literals`, `member_access`,
    `offsets`, `optional_members`, `packages`, `parameterized_types`,
    `pubsub_types`, `rules`, `service_types`, `sql_constraints`,
    `sql_databases`, `sql_tables`, `sql_types`, `sql_virtual_columns`,
    plus more.
  - `others/`, `utils/`, `warnings/`

There are **279 C++ test files** under `test/extensions/language/`. Each
category exercises a slice of the schema language end-to-end (schema
→ generated code → runtime round-trip).

The categories prefixed `sql_*`, `service_*`, `pubsub_*` are out of
scope for zserio-c v1. The rest define our v1 conformance target.

→ Deep dive: [`06-test-corpus.md`](06-test-corpus.md).

## 6. Build infrastructure

- **Ant** is primary. `build.xml` at repo root and one per extension drive
  compilation, jar packaging, test runs.
- **Maven** (`mvn/`) is used only for publishing artifacts to Maven
  Central. Not a primary build path.
- **CMake** (`cmake/`) is used only for the C++ runtime; it is invoked
  from the Ant build for that extension.
- **Scripts** (`scripts/`) wrap the above for CI.

**Implication for our generator integration**: we will not pull Ant or
the upstream Java build into this repo. Phase 2 strategy:
1. CI step builds the upstream compiler JAR once from the submodule.
2. Our generator is built independently as a JAR.
3. CMake invokes `java -cp <upstream-jar>:<our-jar> zserio.tools.ZserioTool`
   to emit C, treating the upstream JAR as a binary artifact.

This keeps our build a pure CMake build with a Java-runtime dependency,
not an Ant-build-of-a-build.

## 7. Upstream's "functional safety" claim vs ours

`compiler/extensions/cpp/README.md` has a "Functional Safety" section.
Read carefully — the bar it describes is high-quality C++11 engineering
practice, but it is materially weaker than what ISO 26262 demands.

What upstream actually does:

- Warnings-as-errors across gcc 7.5+, Clang 11+, MinGW, MSVC 2017+.
- llvm-cov line coverage at ≥99%.
- AddressSanitizer + UBSan clean.
- clang-tidy with a documented suppression file (acknowledges unresolved
  findings, e.g. inability to use C++17 features).
- SonarCloud monitoring.
- 600+ runtime unit tests, 1700+ generated-code tests over 70+ schemas.
- A separate "codeql_autosar" GitHub workflow exists.

What this is **not**:

- Not MISRA C++:2008 / AUTOSAR C++14 conformance — the README does not
  make either claim, and the codebase uses STL exceptions and RTTI which
  AUTOSAR forbids.
- Not 100% line coverage and no mention of branch coverage at all.
- No requirements with stable IDs traced to tests; the tests verify
  behavior, not requirement satisfaction.
- No tool qualification per ISO 26262 part 8.
- No formal safety case, HARA, or item definition.
- C++11 with STL containers means heap behavior is implementation-defined
  and not analyzable for WCET.

zserio-c's functional-safety claim is **materially stronger by design**,
not just in degree:

- C99 (smaller language, smaller certified-compiler matrix).
- MISRA C:2012 with explicit deviations (REQ-FS-001..007).
- Status codes, not exceptions (REQ-FS-002).
- Allocator-injection, not implicit heap (REQ-FS-001).
- Requirements-driven from day one with a CI traceability gate.
- 100% line and branch coverage as a CI gate, not an aspiration.
- ISO 26262 work products grow alongside the code in `docs/safety-case.md`.

This distinction belongs in `README.md` once the safety case has its
first artifact, so the project's positioning is unambiguous.

## 8. Initial implications for zserio-c

These are conclusions we can draw from the survey alone, before any
deep-dive:

1. **v1 schema scope**: defer `sql_*`, `service_*`, `pubsub_*`, JSON I/O,
   reflection, and `walk/validation` tooling. Cover the core schema
   language and wire-format I/O only. Locked precisely in
   `01-schema-language.md`.
2. **Generator integration**: invoke upstream compiler JAR from CMake;
   do not import Ant. (Section 6 above.)
3. **`pmr/` is the closest upstream analog to our allocator-injection
   model.** Read it end-to-end before locking the `zserio_allocator_t`
   ABI. It will inform our vtable shape, alignment handling, and
   container patterns.
4. **Reflection / `ITypeInfo` is out of v1 scope.** Generated code carries
   no runtime type info. This deserves a new requirement; see action
   item below.
5. **Exception elimination is mechanical but pervasive.** Every site in
   upstream C++ that throws `CppRuntimeException` (or its subclasses)
   becomes a `zserio_status_t` return in our port. We will catalog these
   sites in `05-asil-pain-points.md`.
6. **Conformance is sizable but tractable.** ~36 language categories,
   minus the 6–8 deferred groups, leaves roughly 28–30 categories and
   ~200 generated-code tests as the v1 conformance target.
7. **Phase 7 parser cost** (775 lines grammar + 143 Java AST/sema files)
   is realistic but should not anchor v1 estimates.

## 9. Action items from this survey

- [ ] Add `REQ-FS-008 — No runtime reflection in generated code` (no
      `ITypeInfo`-style hooks emitted; struct membership is compile-time
      only).
- [ ] Read `compiler/extensions/cpp/runtime/src/zserio/pmr/` in full as
      input to the runtime API design.
- [ ] In `01-schema-language.md`, decide and record which `language/`
      test categories are in v1 scope vs deferred.
- [ ] In `05-asil-pain-points.md`, enumerate every `throw` site in the
      C++ runtime with citations.

## 10. Next deep-dives, in dependency order

Pre-committed plan, refined with what we now know:

1. `01-schema-language.md` — language constructs; in / deferred / out
   of scope. Master scope list for everything that follows.
2. `02-wire-format.md` — bit layout per construct; anchored in
   `doc/ZserioEncodingGuide.md`, the grammar, and the cpp runtime
   reader/writer.
3. `03-plugin-spi.md` — ZserioExtension SPI: extension entry point, AST
   visitor surface, FreeMarker conventions.
4. `04-cpp-runtime-internals.md` — module decomposition, hot paths,
   `pmr/` allocator design end-to-end.
5. `05-asil-pain-points.md` — concrete enumeration with file/line
   citations.
6. `06-test-corpus.md` — corpus structure, conformance reuse plan.
