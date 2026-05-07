# 03 · Plugin SPI — extending the upstream zserio compiler

**Purpose.** Describe the Java extension surface that Phase 2 will target.
At Phase 2, our generator is a Java JAR that the upstream `ZserioTool`
loads via Java's `ServiceLoader` mechanism, walks the AST through, and
emits C99 source files from. This document specifies what that JAR has
to look like.

**Authoritative sources** (at pinned tag `v2.18.1`):

- `compiler/core/src/zserio/tools/Extension.java` — the SPI itself.
- `compiler/core/src/zserio/tools/ExtensionManager.java` — discovery and lifecycle.
- `compiler/core/src/zserio/tools/ZserioExtensionParameters.java`,
  `compiler/core/src/zserio/tools/ExtensionParameters.java` — parameter passing.
- `compiler/core/src/zserio/extension/common/` — shared helper classes
  (DefaultTreeWalker, OutputFileManager, FreeMarkerUtil, etc.).
- `compiler/core/src/zserio/ast/` — the AST node hierarchy (105 files).
- `compiler/extensions/cpp/` — the largest in-tree consumer; our reference
  implementation pattern.

## 1. The SPI in 30 lines

A zserio extension is a class implementing one interface:

```java
package zserio.tools;

public interface Extension {
    String  getName();                 // human-readable label
    String  getZserioVersion();        // zserio core version this extension expects
    String  getExtensionVersion();     // this extension's own version
    void    registerOptions(Options options);                      // Apache CLI
    boolean isEnabled(ExtensionParameters parameters);             // opt-in
    void    check(Root rootNode, ExtensionParameters parameters)   // pre-flight
            throws ZserioExtensionException;
    void    process(Root rootNode, ExtensionParameters parameters) // do work
            throws ZserioExtensionException;
}
```

That is the entire surface. Everything else is convention and helpers.

## 2. Discovery and lifecycle

### Discovery

`ExtensionManager` uses Java's `java.util.ServiceLoader` to find
implementations on the class path. An extension JAR therefore ships one
file:

```
META-INF/services/zserio.tools.Extension
```

containing the fully-qualified class name(s) of the implementation, one
per line. The reference C++ extension's file contains a single line:

```
zserio.extension.cpp.CppExtension
```

For zserio-c, ours will read:

```
zserio.extension.c.CExtension
```

### Loading and version match

`ExtensionManager` constructs each registered extension and asks for
`getZserioVersion()`. It compares this against the running zserio
core's version via `ExtensionVersionMatcher`. If the versions are not
compatible, the extension is ignored with a printed warning; the rest
of the tool continues. We tolerate this gracefully by keeping our
expected version in sync with the pinned upstream tag.

### Lifecycle (per CLI invocation)

For each enabled extension, the tool calls in this order:

1. `registerOptions(options)` — once at startup, before parsing CLI
   arguments. Each extension adds its own flags; the CLI library merges
   them.
2. `isEnabled(parameters)` — once after CLI parsing. If the extension's
   characteristic option (e.g. `-c <out>` for us) is not set, the
   extension returns `false` and skips both `check` and `process` for
   this run.
3. `check(rootNode, parameters)` — pre-flight on the AST. Failing
   `check` aborts the run before any output is written. Used for things
   like reserved-keyword clashes against the target language.
4. `process(rootNode, parameters)` — actual code generation.

Both `check` and `process` get the **same** parsed AST (`Root`), so
multiple extensions can run in one invocation (e.g. cpp + python in
the same call) without re-parsing.

## 3. The AST surface

`Root` (`compiler/core/src/zserio/ast/Root.java`) is the entry node.
It exposes two traversal mechanisms:

### 3.1 Low-level visitor — `ZserioAstVisitor`

Method per node type. Useful when you need to visit *every* AST node
(e.g. expression sub-trees inside fields). Used by the core's own
semantic checks and by `PackedTypesCollector`. Most extensions don't
implement this directly; they use the higher-level walker below.

`ZserioAstDefaultVisitor` provides empty implementations for all
methods so subclasses override only the ones they care about.

### 3.2 High-level walker — `TreeWalker`

(`compiler/core/src/zserio/extension/common/TreeWalker.java`)

Methods correspond to top-level **language entities**:

```java
public interface TreeWalker {
    boolean traverseTemplateInstantiations();

    void beginRoot(Root root);              void endRoot(Root root);
    void beginPackage(Package pkg);         void endPackage(Package pkg);
    void beginImport(Import importNode);
    void beginConst(Constant constant);
    void beginRuleGroup(RuleGroup ruleGroup);
    void beginSubtype(Subtype subType);
    void beginStructure(StructureType structureType);
    void beginChoice(ChoiceType choiceType);
    void beginUnion(UnionType unionType);
    void beginEnumeration(EnumType enumType);
    void beginBitmask(BitmaskType bitmaskType);
    void beginSqlTable(SqlTableType sqlTableType);
    void beginSqlDatabase(SqlDatabaseType sqlDatabaseType);
    void beginService(ServiceType serviceType);
    void beginPubsub(PubsubType pubsubType);
    void beginInstantiateType(InstantiateType instantiateType);
}
```

`DefaultTreeWalker` in `extension/common` provides empty defaults.

For zserio-c, **the only `begin*` callbacks we will implement** are:

- `beginRoot` / `endRoot` (bookkeeping)
- `beginPackage` / `endPackage` (output directory management)
- `beginConst`, `beginSubtype`, `beginStructure`, `beginChoice`,
  `beginUnion`, `beginEnumeration`, `beginBitmask`,
  `beginInstantiateType`

Out of v1 scope (per `01-schema-language.md`): `beginSqlTable`,
`beginSqlDatabase`, `beginService`, `beginPubsub`, `beginRuleGroup`.
We provide no-op overrides for these or just leave the defaults.

### 3.3 AST node taxonomy

The 105 files under `compiler/core/src/zserio/ast/` partition into:

| Group           | Examples                                                      |
|-----------------|---------------------------------------------------------------|
| Top-level entities | `StructureType`, `ChoiceType`, `UnionType`, `EnumType`, `BitmaskType`, `Subtype`, `Constant` |
| Member declarations | `Field`, `EnumItem`, `BitmaskValue`, `Function`, `Parameter`, `ChoiceCase`, `ChoiceDefault` |
| Type instantiations | `ArrayInstantiation`, `DynamicBitFieldInstantiation`, `TypeInstantiation`, `InstantiateType` |
| Built-in types  | `BooleanType`, `BytesType`, `BuiltInType`, `ExternType`, `FixedBitFieldType`, `FloatType`, `StringType`, `StdIntegerType` |
| Expressions     | `Expression`, `ExpressionIntegerValue`, `ExpressionComparator`, `ExpressionUtil` |
| Doc comments    | `DocComment`, `DocCommentClassic`, `DocCommentMarkdown`, `DocText`, `DocTagDeprecated`, `DocTagParam`, `DocTagSee`, `DocTagTodo`, `DocParagraph` |
| Locations       | `AstLocation`, `Package`, `PackageName`, `Import` |
| Walkers / visitors | `ZserioAstVisitor`, `ZserioAstWalker`, `ZserioAstTreeWalker`, `ZserioAstDefaultVisitor`, `ZserioParseTreeChecker` |

We touch the first three groups heavily; expressions through the
`ExpressionFormatter` helper (§5); doc comments selectively (§6.4 below).

## 4. The standard generator pattern

Reading `compiler/extensions/cpp/src/zserio/extension/cpp/CppExtension.java`
gives the canonical structure. Our extension follows the same:

```java
public final class CExtension implements Extension {

    @Override public String getName() { return "C99 Generator"; }
    @Override public String getZserioVersion() { /* matches pinned tag */ }
    @Override public String getExtensionVersion() { /* our version */ }

    @Override public void registerOptions(Options options) {
        CExtensionParameters.registerOptions(options);
    }

    @Override public boolean isEnabled(ExtensionParameters p) {
        return CExtensionParameters.hasOptionC(p);
    }

    @Override public void check(Root rootNode, ExtensionParameters p) {
        rootNode.walk(new CompatibilityChecker());
        rootNode.walk(new ReservedKeywordsClashChecker("C", C_KEYWORDS));
        // any zserio-c-specific checks (e.g. forbidden-feature gate)
    }

    @Override public void process(Root rootNode, ExtensionParameters p) {
        OutputFileManager files = new OutputFileManager(p);
        CExtensionParameters cp = new CExtensionParameters(p);

        PackedTypesCollector packed = new PackedTypesCollector();
        rootNode.accept(packed);

        List<CDefaultEmitter> emitters = new ArrayList<>();
        emitters.add(new ConstEmitter(files, cp, packed));
        emitters.add(new SubtypeEmitter(files, cp, packed));
        emitters.add(new EnumerationEmitter(files, cp, packed));
        emitters.add(new BitmaskEmitter(files, cp, packed));
        emitters.add(new StructureEmitter(files, cp, packed));
        emitters.add(new ChoiceEmitter(files, cp, packed));
        emitters.add(new UnionEmitter(files, cp, packed));

        for (CDefaultEmitter emitter : emitters)
            rootNode.walk(emitter);

        files.printReport();
    }
}
```

The pattern in plain English:

1. One **emitter** class per top-level construct kind (constants,
   subtypes, structs, choices, …).
2. Each emitter is a `DefaultTreeWalker` that overrides the relevant
   `begin*` callback.
3. The `begin*` callback:
   - Builds a denormalized **`*TemplateData`** POJO from the AST node.
     This is the model FreeMarker sees.
   - Calls the helper that runs FreeMarker on a template file with
     that POJO, writing the output through `OutputFileManager`.
4. The whole AST is walked **once per emitter** (eight walks for the
   eight emitters above). This is wasteful but simple; perf is fine
   for compiler-time costs.
5. `OutputFileManager` deduplicates write-with-identical-content
   (avoiding spurious file mtimes that would re-trigger downstream
   builds).
6. `PackedTypesCollector` is run as an `AstVisitor` (low-level) before
   the emitter walks, marking types that are used as elements in any
   `packed` array. This drives whether each type's emitted code
   includes the `DeltaContext` machinery.

### Single base class

`compiler/extensions/cpp/src/zserio/extension/cpp/CppDefaultEmitter.java`
shows the per-extension base: it extends `DefaultTreeWalker` and adds
helpers like `processHeaderTemplate(...)`, `processSourceTemplate(...)`.
We do the same with `CDefaultEmitter`.

## 5. Common helpers (use, don't reinvent)

These live in `zserio.extension.common` and we should consume them:

- **`DefaultTreeWalker`** — base for all emitters.
- **`OutputFileManager`** — buffered file output with deduplication and
  a per-extension report.
- **`FreeMarkerUtil`** — runs a named FreeMarker template on a data
  model and writes the result. We don't construct a FreeMarker
  `Configuration` directly; we go through this.
- **`ReservedKeywordsClashChecker`** — generic against any keyword set.
  Our `check()` constructs one with the C99 keyword list (shorter than
  C++; see §8.3).
- **`CompatibilityChecker`** — flags schema constructs that the pinned
  zserio core version does not support. Useful to fail-fast cleanly.
- **`PackedTypesCollector`** — discovers which user-defined types appear
  as elements of a `packed` array. Required for §5 of `02-wire-format.md`
  to emit `DeltaContext` machinery only where needed.
- **`ExpressionFormatter`** + **`ExpressionFormattingPolicy`** — turns a
  zserio expression AST into a target-language expression string.
  We will subclass `DefaultExpressionFormattingPolicy` to produce C99
  expressions: identifiers go to C99 names, member-access becomes `->`
  or `.`, builtin operators get C99 spellings.
- **`StringEscapeConverter`** — escape zserio string literals into C
  string literal form.
- **`FileUtil`** — directory / path helpers for output organization.

## 6. FreeMarker conventions

### 6.1 Template inventory

Per-extension templates live in `compiler/extensions/<name>/freemarker/`
as resources packaged into the JAR. The cpp extension has 32 `.ftl`
files; ours will have a similar count.

Convention:
- `Foo.h.ftl` — emits a header file for top-level construct `Foo`
- `Foo.cpp.ftl` — emits an implementation file for `Foo`
- `Bar.inc.ftl` — partial / shared snippet, included from other
  templates via `<#include "Bar.inc.ftl">`

For zserio-c, we will have:

```
generator/templates/
├── FileHeader.inc.ftl         (banner, include guards, generator banner)
├── DocComment.inc.ftl         (doc-comment rendering)
├── CommonField.inc.ftl        (field declaration / read / write snippets)
├── CommonInit.inc.ftl         (per-type init / free helpers)
├── Constant.h.ftl
├── Subtype.h.ftl
├── Enumeration.h.ftl   Enumeration.c.ftl
├── Bitmask.h.ftl       Bitmask.c.ftl
├── Structure.h.ftl     Structure.c.ftl
├── Choice.h.ftl        Choice.c.ftl
├── Union.h.ftl         Union.c.ftl
└── DeltaContext.inc.ftl       (packed-array code, included by per-type templates)
```

Notably **no** Pubsub / Service / Sql templates — out of v1 (per
`01-schema-language.md`). And **no** Reflectable / TypeInfo templates —
forbidden by REQ-FS-008.

### 6.2 Template data POJOs

Each `*Emitter` builds a `*EmitterTemplateData` POJO. The cpp pattern:

- `StructureEmitterTemplateData` — top-level data passed to
  `Structure.h.ftl` / `Structure.cpp.ftl`.
- `CompoundFieldTemplateData` — per-field data, used inside the
  compound templates.
- `CompoundConstructorTemplateData`, `CompoundFunctionTemplateData`,
  `CompoundParameterTemplateData` — sub-aspects of a compound type.
- `ArrayTraitsTemplateData` — describes what to emit for an array
  field (which traits class to use in C++; for us, which per-element
  encode/decode call to emit).
- `RuntimeFunctionTemplateData` — names of runtime functions per
  primitive type, so the template can splice them in without
  hardcoding.
- `DocCommentsTemplateData` — doc-comment rendering data.
- `TemplateDataContext` — shared context object passed through, holds
  cross-cutting concerns (extension version, generator description,
  packed-types map).

This denormalization step is where our ASIL-friendly choices crystallize:
no `ITypeInfo`, no `Reflectable`, no `Pubsub`/`Service`/`Sql` data.
Per-construct generator output is therefore considerably narrower than
upstream's.

### 6.3 Rendering call

Inside an emitter:

```java
@Override
public void beginStructure(StructureType structureType) {
    StructureEmitterTemplateData data = new StructureEmitterTemplateData(
            getTemplateDataContext(), structureType, getPackedTypesCollector());
    processHeaderTemplate("Structure.h.ftl", data, structureType);
    processSourceTemplate("Structure.c.ftl", data, structureType);
}
```

`processHeaderTemplate` and `processSourceTemplate` are inherited from
`CDefaultEmitter` and ultimately call `FreeMarkerUtil.processTemplate(...)`,
writing through `OutputFileManager`. The output filename derives from
the schema package (`a.b.Foo` → `a/b/foo.h` and `a/b/foo.c`).

### 6.4 Doc comments

The AST attaches `DocComment` nodes to documentable entities (types,
fields, members). These come in two flavors — `DocCommentClassic`
(Javadoc-like) and `DocCommentMarkdown`. Both flatten to a sequence of
`DocParagraph` → `DocLineElement`s.

For zserio-c we render doc comments to the generated C headers using
straightforward `/** … */` blocks, with `@deprecated`, `@param`,
`@see`, `@todo` tags either preserved or stripped depending on a
generator flag (default: preserve).

## 7. Build and packaging

### 7.1 Upstream pattern

Each extension is built with **Ant** (`compiler/extensions/<name>/build.xml`).
The Ant target compiles Java sources, packages classes + templates +
META-INF into a JAR named e.g. `zserio_cpp.jar`, and places it in
`compiler/extensions/<name>/build/jar/`.

### 7.2 Our pattern (Phase 2)

We do **not** pull Ant into our own repo (decided in `00-survey.md` §6).
Instead, the `generator/` subdir of this repo holds:

- `generator/build.gradle.kts` (or `pom.xml`) — Gradle/Maven build that
  produces `zserio_c.jar`.
- `generator/src/main/java/zserio/extension/c/` — Java sources.
- `generator/src/main/resources/META-INF/services/zserio.tools.Extension`
  — single line: `zserio.extension.c.CExtension`.
- `generator/src/main/resources/freemarker/` — `.ftl` templates,
  packaged as JAR resources.

CMake (top-level) wires the build: a custom target invokes Gradle to
build the JAR, and emit-time targets invoke the upstream `ZserioTool`
with both JARs on the classpath:

```
java -cp ${ZSERIO_TOOL_JAR}:${ZSERIO_C_JAR} zserio.tools.ZserioTool \
    -src <schema_root> <main.zs> \
    -c <output_dir>
```

The upstream JAR is built once from the submodule, cached, and treated
as a binary artifact — we never modify it.

### 7.3 Build dependency: which upstream artifacts do we need at compile time?

Our generator JAR's `compileClasspath` needs:

- **`zserio_core.jar`** — exposes the AST classes (`Root`, `Package`,
  `StructureType`, `Field`, `Expression`, …), `Extension` interface,
  `extension.common.*` helpers.
- **`freemarker.jar`** — transitively, since we use FreeMarker templates.
  Pulled in by `zserio_core.jar`'s dependencies.
- **`commons-cli`** — for `Options`. Same.

At runtime our JAR is loaded into the same JVM as the core, so we
don't repackage these dependencies; we declare them as `provided`
(Maven) / `compileOnly` (Gradle).

## 8. Implications for our generator (Java surface)

### 8.1 Package and entry class

```
zserio.extension.c.CExtension                  // Extension impl
zserio.extension.c.CExtensionParameters        // CLI options
zserio.extension.c.CExtensionVersion           // version constants
zserio.extension.c.CDefaultEmitter             // emitter base
zserio.extension.c.<Foo>Emitter                // per-construct emitters
zserio.extension.c.<Foo>EmitterTemplateData    // per-construct template data
zserio.extension.c.TemplateDataContext         // shared context
zserio.extension.c.CExpressionFormattingPolicy // C-language expressions
zserio.extension.c.IncludeCollector            // tracks #include needs
zserio.extension.c.RuntimeFunctionTemplateData // runtime fn name registry
```

### 8.2 CLI options

Minimum set:

- `-c <output_dir>` — output directory; presence of this option is
  what `isEnabled` keys off.
- `-cWithCodeComments` — preserve doc comments in generated headers
  (default on).
- `-cWithSourcesAmalgamation` — emit one `<package>.c` per package
  instead of one `.c` per type, similar to upstream's
  `-withSourcesAmalgamation`. Default off.
- `-cWithRangeChecks` — emit runtime range checks for bit fields
  (REQ-FS-006 contract). Default on.

We deliberately do NOT add `-cWithReflection`, `-cWithJsonCode`,
`-cWithTypeInfo` — those are out of v1 scope per
`01-schema-language.md`.

### 8.3 C99 reserved keywords

`ReservedKeywordsClashChecker` needs the C99 keyword set plus our
runtime-prefixed identifiers. C99 keywords (much shorter than C++):

```
auto break case char const continue default do double else enum
extern float for goto if inline int long register restrict return
short signed sizeof static struct switch typedef union unsigned
void volatile while _Bool _Complex _Imaginary
```

Plus reserved by our runtime: identifiers starting with `zserio_`,
`ZSERIO_`. Our checker rejects schema identifiers that map to either
set in the generated C name.

### 8.4 Identifier mapping

Convention for translating zserio names to C99:

- Type names: `lower_snake_case` with the package as a prefix derived
  from the package path. `pkg.foo.MyType` → `pkg_foo_my_type_t`.
- Field names: `lower_snake_case`. `myField` → `my_field`.
- Enum members: `UPPER_SNAKE_CASE`. `RED_FOO` → `RED_FOO` (kept as-is
  if already conformant).
- Generated functions: `<type_name>_<verb>` per the runtime convention
  in `02-wire-format.md` §8.

### 8.5 Expression formatter

`CExpressionFormattingPolicy` translates zserio expressions to C99:

- Member access: `a.b.c` → `a.b.c` for value paths, `a->b->c` for
  pointer paths. The formatter walks the AST type info to pick.
- Operator spellings: most are identical (`+`, `-`, `*`, `/`, `&`, `|`).
  `lengthof(arr)` → `arr.size` (arrays generate a struct with `data` /
  `size`).
- Type casts: introduce explicit C casts where the zserio expression
  changes width.
- Constants: refer to `static const` declarations emitted from the
  schema's `const` block.

Formatter details get worked out during Phase 2 implementation; the
SPI-level decision is just that we use `ExpressionFormatter` (don't
reinvent).

### 8.6 What we don't need from upstream's helpers

- **`DefaultExpressionFormattingPolicy`** offers SQL hooks — irrelevant
  for us; we override and skip them.
- **`SqlNativeTypeMapper`**, the `extension/common/sql/*` package — out
  of v1.

## 9. Open questions / Phase 2 design choices

These do not affect the SPI contract but they will affect the generator's
internal structure. Resolve when Phase 2 starts.

1. **Build tool**: Gradle (Kotlin DSL) vs. Maven. Recommend Gradle —
   modern, widely supported, easy to integrate with the upstream JAR
   as a flat-file dependency, and simpler for the kind of monolithic
   Java project we'll have.
2. **JDK target**: Java 8 vs. 17. Upstream's pinned tag uses Java 8
   features comfortably. Recommend Java 11 minimum for our generator
   (small modernization gain) provided the upstream build still
   resolves; otherwise stick to 8.
3. **Code-style enforcement**: Spotless + a Google-Java-Format profile
   in CI. Lighter touch than the upstream Spotbugs / Checkstyle stack
   but enough for our team size.
4. **Per-package vs. per-type C output**: see the
   `-cWithSourcesAmalgamation` flag in §8.2. Per-type by default
   matches upstream and gives finer-grained rebuild; amalgamation
   reduces compile time at the cost of less-incremental rebuilds.
   Both modes wired now to avoid retrofit later.
5. **Emit order**: upstream walks the AST once per emitter (`O(emitters
   × types)`). Acceptable for a compile-time tool. If we ever need to
   speed it up, a single walker dispatching to all emitters is a
   one-day refactor — but we should NOT premature-optimize. Start with
   the upstream pattern.

## 10. Acceptance

This document is *accepted* when:

- [x] The `Extension` interface is documented end-to-end (§1, §2).
- [x] AST traversal mechanism described, with the v1 callback set
      enumerated (§3).
- [x] The generator structure pattern is documented with a concrete
      `CExtension` skeleton (§4).
- [x] Common helpers are catalogued so Phase 2 doesn't reinvent (§5).
- [x] FreeMarker conventions and our expected template inventory are
      fixed (§6).
- [x] Build and packaging strategy is decided — no Ant in our repo,
      Gradle JAR plus CMake-orchestrated invocation of the upstream
      tool (§7).
- [x] Open Phase 2 design choices are recorded but not yet locked (§9).
