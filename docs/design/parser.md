# Parser design — scope and decision record

The scoping analysis and decision log (Q1..Q8 below) that shaped
the C-native parser/generator (`zserio_c_gen`) under `parser/`.
The generator is **built and in production use**: it is the C-side
generator for the per-commit xvalidation gate (see
`tests/xvalidation/run_xvalidation.sh` and the `xvalidation` CI
job). This document is kept as the rationale record the source
comments cite (e.g. `parser/src/parser.c` cites Q5 for the
recursion deviation, `parser/include/zsparser/ast.h` cites Q2 for
the arena-allocated AST). The body preserves the analysis as
written at scoping time (2026-05); where the as-built outcome
differs, an explicit **as-built** note says so. The phasing table
and success criteria at the end record current status.

## Goal

Replace the Java front-end (upstream `zserio_core.jar` +
`generator/zserio_c.jar`) with a C-native parser + code emitter, so
that the entire zserio-c toolchain can be built with a single C99
toolchain and qualified as a single TQ-2 subject.

**Status: achieved for the C build path.** `zserio_c_gen` is the
only generator on the C target; a C99 toolchain alone builds it. The
Java pieces remain in the tree solely to drive the xvalidation
oracle and its sibling gates (see "Phasing — status" below).

## Why (rationale at scoping time)

- **Toolchain qualification surface.** At scoping time the build
  chain was
  `java → ant → upstream zserio_core.jar → our generator extension
  jar → FreeMarker → emitted C → C compiler`. Every link is in scope
  for ISO 26262-8 Cl 11.4.6 toolchain qualification. The JVM, ANT,
  and the upstream Java AST are large pieces of third-party
  machinery that we currently qualify by validation (the 182-schema
  xvalidation gate). A C-native pipeline collapses this to
  `C parser → C emitter → C compiler` — three components, all in
  the same language, all under our own MISRA and coverage gates.
- **Build hermeticity.** A C99 toolchain is universally available on
  embedded-CI hosts. The JVM is not. Several integrators have asked
  for a Java-free build path.
- **Generator quality.** The current generator's templating layer
  (FreeMarker) is the load-bearing thing — most generator features
  live in the template body. A C-native emitter lets us write the
  same logic in code we can step through, fuzz, and coverage-
  measure, instead of in a templating language.
- **Phase 6 closure.** Runtime + generated-code optimization is in;
  every remaining win is in the schema → C path, which the Java
  front-end constrains.

## In scope

1. **C-native schema parser.** Tokenizer + parser for the upstream
   zserio schema language (`.zs`), producing an internal AST. Must
   accept every schema in the upstream `tests/` corpus + every
   schema in the 182-schema xvalidation set.
2. **Semantic analyzer.** Name resolution, type checking, expression
   evaluation, choice-resolution analysis, packed-array eligibility —
   everything upstream's `compiler/core` AST passes do today.
3. **C code emitter.** Walks the analyzed AST and emits the C files
   the current FreeMarker templates emit, byte-equal modulo
   whitespace + comments.
4. **CLI compatibility.** Same `-src`, `-c`, etc. flags as today, so
   downstream `CMakeLists.txt` / `build_generator_jar.sh` callers
   only swap the executable name, not the invocation.
5. **Per-allocation array-length sanity bound** (carried in from the
   2026-05-15 memory-allocation discussion). The generated reader
   for any variable-length array must, before calling `alloc`,
   verify that the claimed element count is consistent with the
   bits remaining in the stream. Mirrors the existing hand-written
   bound in `runtime/src/string.c:103`. Closes the asymmetry called
   out under "One real gap worth flagging" in the planning notes.
   Even though this could have been done in the Java generator,
   doing it as part of the C-native generator's first cut keeps the
   change localized and derives the bound from a single source of
   truth (the analyzer, not a copy of it). Addresses the
   unbounded-memory-allocation-in-deserialization vulnerability
   class patched upstream in v2.18.1 ("validate varsize against
   stream size"); upstream `zserio-cpp17` carries the equivalent
   check.

   **As built:** `zserio_c_gen` emits two pre-allocation guards per
   wire-driven array (`emit_array_alloc_guard` in
   `parser/src/emit.c`): an overflow guard rejecting counts whose
   `count * sizeof(element)` would wrap `size_t`, and a bomb guard
   rejecting counts larger than the bits remaining in the stream
   (every unpacked element occupies at least one bit). Both return
   `ZSERIO_ERR_ARRAY_TOO_LARGE` (REQ-FS-005 / REQ-FN-017).
   Exercised by `tests/parser/test_decode_hardening.c` and the
   decode-path fuzz job.

6. **Defense-in-depth initial-allocation caps** against the second
   attack surface in the same vulnerability class: a "wire-level
   plausible-but-huge" claim that passes the remaining-bits check
   but still triggers a large upfront allocation (e.g., a 100 MB
   stream legitimately claiming 100M 1-byte elements). The design
   scoped here followed the upstream cpp v2.18.1 fix shape — a
   configurable byte cap on the initial allocation plus amortized
   doubling growth, carried as compile-time defaults and
   per-allocator cap fields — so that peak allocation is bounded by
   2× *actually validated* elements rather than by the attacker's
   claim. Defaults were to be tuned for large production automotive
   map payloads. The full mechanism is preserved in the Q8 decision
   record below.

   **As built: not adopted.** `zserio_allocator_t` carries no cap
   fields, and no `REQ-FS-009` was created (the safety requirements
   end at REQ-FS-008). The shipped defense is item #5's guard pair,
   which bounds any allocation by what the stream's actual length
   can justify — a tiny stream cannot trigger a huge allocation; the
   worst case is bounded by the input size times the element width.
   Arena deployments additionally get a hard peak-memory bound from
   the arena's own capacity. The byte-cap + doubling-growth design
   remains available in Q8 if a workload emerges for which the
   stream-length bound is too permissive.

7. **Toolchain qualification update.** New `docs/safety-case/tool-
   qualification-parser.md`, supersede `tool-qualification-
   generator.md`. Coverage gate, MISRA gate, and fuzz target for
   the parser. Records the Q5 position (MISRA strict on parser +
   analyzer + emitter with the Rule 17.2 deviation for recursive
   descent) and the deviation policy (prefer bounded documented
   deviations over Java fallback or permissive C).

   **As built: open.** `docs/safety-case/tool-qualification-generator.md`
   still documents the Java extension; the qualification write-up for
   `zserio_c_gen` (and the MISRA gate on the parser's own sources —
   today the parser source is held to the strict C99 `-Werror`
   warning gate, not the cppcheck MISRA gate) is an open work item.

## Out of scope (for Phase 7)

- Schema-language extensions. We track upstream's language; we do not
  add our own dialect.
- Multi-target codegen (Java/Python/etc.). The Java front-end will
  remain available to upstream — we only need to replace it for the
  C target.
- Removing the third-party zserio submodule. We still vendor it for
  the upstream test corpus and the xvalidation oracle.
- A new IR. The analyzer walks the AST directly; we do not
  introduce a separate optimization IR.

## Design questions (all resolved — see the decisions log)

These were the calls to make before writing the first parser TU.
Each was a real decision with cost/benefit either way; the resolved
outcome and rationale for each is in the decisions log at the end of
this document.

### Q1 — Parser construction approach

Options:

- **Hand-written recursive descent + Pratt for expressions.** Gives
  us a MISRA-compliant code path end to end, no third-party parser
  generator, full control over error recovery and source-location
  fidelity. ~3–5 kLOC of careful C. Mirrors how clang, lua, and
  most modern hand-written parsers are built.
- **ANTLR4 C target.** Matches upstream grammar 1:1; the `.g4`
  files are 482+160 LOC and would generate ~10 kLOC of C from
  ANTLR4's C runtime. But ANTLR4's C runtime is a substantial third-
  party dep that itself enters TQ scope, which defeats part of the
  point.
- **Lemon / re2c / byacc.** Smaller generators with less runtime;
  re2c is appealing for the lexer. Still adds a TQ-2 dep on the
  generator binary itself, but the generator output is more
  inspectable than ANTLR's.

Recommendation: **hand-written**. The grammar is small enough,
control over diagnostics matters, and it removes any third-party
parser-toolkit from TQ scope. Suggested mitigation for the
"hand-written drifts from spec" risk: a small property-based fuzz
target that generates random grammar-conforming schemas via the
upstream grammar and asserts our parser accepts them — exists
today in spirit via the 182-schema corpus, but we could extend.

### Q2 — AST shape

Options:

- **Tagged union per node kind** (`enum kind; union { ... }`).
  Compact, C-idiomatic, MISRA-friendly with a small per-kind
  visitor.
- **Discriminated record + child pointer list.** More uniform but
  pays a `kind + len + children[]` overhead per node.
- **Tree of typed structs with explicit parent links** (matches
  upstream Java AST). Easiest port from upstream node names but
  least C-idiomatic.

Recommendation: **tagged union**. Roughly 30–50 distinct node kinds
once collapsed (upstream's 105 includes a lot of trivial wrappers we
don't need to mirror). Arena-allocate everything to a single
schema-scoped arena; parser owns the arena, hands it to the
analyzer, and the emitter walks it read-only. Frees the entire AST
in O(1) at end of compile.

### Q3 — Code emission

Options:

- **Hand-written emission as a tree-walking print pass.** Each AST
  node has an `emit_decl` / `emit_def` pair. No templating layer.
  Cleanest, most debuggable, biggest line count.
- **Minimal embedded template system in C.** Snippet table indexed
  by node kind; placeholder substitution at emit time. Saves some
  lines but reintroduces a templating-style indirection.
- **Keep FreeMarker, drive it from the C side.** Parser emits a
  serialized AST that a small Java shim runs templates over.
  Half-transition; doesn't deliver the build-hermeticity goal.

Recommendation: **hand-written emission**. Once the templates are
expanded the code we produce is fairly regular; the bulk of the
current FreeMarker complexity is conditionals, not formatting. A
disciplined emitter API (`emit_lit`, `emit_indent`, `emit_block`)
keeps the line count down.

### Q4 — Migration strategy

Options:

- **Parallel run, byte-equal cutover.** Build the C parser
  alongside the Java jar. CI runs both; xvalidation gate requires
  byte-equal output (modulo whitespace + comments). Cutover when
  all 182 schemas pass for N consecutive commits. Java jar deleted
  on cutover.
- **Schema-by-schema cutover.** Whitelist of schemas the C parser
  owns; rest go to the Java jar. Whitelist grows over time. Risk:
  test split, harder to keep both paths green.
- **Big-bang cutover.** Land C parser, delete Java jar in the same
  PR. Highest risk; needs an exceptionally thorough pre-merge
  validation. Not recommended for a safety-relevant project.

Recommendation: **parallel run with byte-equal cutover**. Costs:
duplicate CI cycles for the duration. Benefits: regression risk
collapses to "did we observe a divergence", which the existing
xvalidation harness already catches.

### Q5 — Where MISRA applies

The parser is a host-side tool, not on-target code. Three positions
are defensible:

- **MISRA strict on parser + analyzer + emitter.** Same gate as the
  runtime. Headline friction: Rule 17.2 (no recursion) and Rule
  17.1 (`<stdarg.h>` in emit helpers). Realistic effort tax: +20–30%
  vs permissive C.
- **MISRA strict on emitter output only.** Parser/analyzer source
  under `-Wall -Wextra -Werror` but not MISRA. Aligns with how the
  Java front-end is qualified today. Cheapest option.
- **Hybrid.** Emitter source under MISRA strict; parser/analyzer
  not. Three different gates in one toolchain.

The case for strict (option A) is the safety argument for the
rewrite itself: under option B the safety story is essentially
unchanged from today's Java-front-end + qualified-by-validation
model. The Phase 7 rewrite's safety value comes from holding the
toolchain to the same standard as the runtime; option B captures
only the build-hermeticity value, not the safety value. MISRA +
validation stack rather than substitute — they catch different
classes of bug.

**Decision: option A** with one up-front documented deviation
(Rule 17.2) and a policy that future MISRA frictions are resolved
by **adding bounded, documented deviations** rather than by
falling back to permissive C or to a Java half-step. Effort tax
absorbed; deviations land as siblings under `docs/misra-deviations/`
with the same evidence shape as D001–D006.

*(As built: the parser source follows the option-A coding
discipline — no `<stdarg.h>`, allocator-vtable memory, bounded
recursion — and is held to the strict C99 `-Werror` warning gate,
but the cppcheck MISRA gate in CI currently covers only
`runtime/` and generated output. Wiring `parser/src/` into the
MISRA gate is an open item, tracked with the qualification
write-up under in-scope item #7.)*

Initial deviation list (grows as implementation proceeds):

- **Rule 17.2** — recursive functions in the parser. Bounded by
  grammar max-nesting depth; stack-budget gate (REQ-FS-003)
  closes the residual concern.

Frictions resolved by workaround, not deviation:

- **Rule 17.1** (`<stdarg.h>`) — typed emit helpers
  (`emit_lit`, `emit_ident`, `emit_block_open`...) rather than
  `emit_fmt`. ~10–15% emit-API verbosity tax.
- **Rule 21.x** (libc) — already-established discipline. String
  interning via the allocator vtable; no `strdup`/`sprintf`.
- **Memory model** — arena-allocated AST flows through
  `zserio_allocator_t`, identical to runtime patterns.

### Q6 — Source locations + diagnostics

Decision needed early because it shapes every AST node:

- Carry `(file_id, line, col)` per node? (cost: 8–12 bytes per
  node; benefit: usable diagnostics)
- Carry a `(file_id, source_offset)` pair only, recover line/col
  on diagnostic emission? (cost: O(log file_length) per diagnostic;
  benefit: 4 bytes per node)
- Drop locations entirely after AST construction? (cost: bad
  diagnostics from the analyzer; benefit: smallest nodes)

Recommendation: **`(file_id, source_offset)` pair**. Line+col is
expensive to track during lex and not what we use during analysis;
we want it only when emitting a diagnostic. Lookup is a single
binary search per diagnostic over a line-start table built once per
file.

### Q7 — Data/View split for parameterized types

Upstream `zserio-cpp17` (`doc/Cpp17Design.md:163`) introduces a
`View<Data>` accessor wrapper: the Data struct is a POD-style owning
container, the View carries the schema-evaluated parameters by
value plus a `const Data&` reference. Solves the two-phase-init
problem that parameterized types create today — in the old C++
generator (and in our current generator) parameters live as struct
members, and a `_init`-style call has to be made before `_read` to
populate them.

Options:

- **Adopt the Data/View split.** Cleaner handling of parameterized
  types, no `_init` shim required, immutable accessor pattern is
  genuinely nicer ergonomically. Cost: roughly 2× the generated
  code per compound type (Data + View per node), more complex
  emitter logic, public-API shape change that ripples through every
  consumer of our generated headers.
- **Keep parameters as struct members + `_init` style.** Simpler,
  less generated code per compound, existing 182-schema xvalidation
  harness already validates this works.

Decision: **don't adopt for Phase 7**. Tracked under "Future
considerations" (FC-01) rather than as a named follow-on phase —
no commitment that the change will ever be made; re-evaluated only
on a concrete trigger. The split is a worthwhile idea but it's an
API-shape change with broad downstream impact, and Phase 7 already
carries enough scope (full parser + analyzer + emitter rewrite).
The explicit reason this is recorded rather than silently passed
over: upstream cpp17 has converged on the View pattern for good
reasons, and a future reader of the safety case should see that we
considered it deliberately rather than missed it.

Memory-model implication: **none**. View is pointers-into-tree, not
a layout change. Adopting or not adopting Views has zero effect on
the contiguous-chunk question — see the 2026-05-15 memory-layout
discussion notes.

### Q8 — Initial-allocation cap shape

*(As built, this design was not adopted — see the as-built note on
in-scope item #6. The analysis is preserved as the record of what
was considered and why.)*

The upstream unbounded-memory-allocation-in-deserialization
vulnerability class (patched upstream in v2.18.1) documents two
distinct attack surfaces:

1. **Wire-level lie** — claimed array length exceeds buffer.
   Closed by item #5 (remaining-bits sanity bound).
2. **Wire-level plausible-but-huge** — claim fits the buffer but is
   enormous (e.g., 100 MB stream legitimately claiming
   100M 1-byte elements). PoC numbers from the upstream advisory:
   a 4-byte payload → 762 MB alloc (amplification 200,000,000×); a
   5-byte payload → 16 GB / system crash.

Item #5 alone closes (1); (2) needs a separate defense.

Industry alignment on (2):

- **Upstream cpp v2.18.1** (patch `16f1b8c34`): `ArrayPreallocation`
  wrapper class with `MAX_INITIAL_ARRAY_ALLOCATION = 128 * 1024`
  byte cap. Reader stores it; `Array.h:readImpl` does
  `reserve(min(claim, cap/sizeof(element)))`, then
  `std::vector::push_back` grows the vector via amortized
  doubling as elements are read in. Companion checks: stream-
  bound (`pos + claim * elementSize <= bufferBitSize`).
- **Upstream cpp17**: had `maxArrayPrealloc` in design before the
  advisory; same shape.
- **`woven-by-toyota/go-zserio`** commit `39ef1de`: env-var-
  configurable element-cap (`ZSERIO_MAX_INITIAL_ARRAY_SIZE =
  1000`) + byte-cap (`ZSERIO_MAX_INITIAL_BLOB_SIZE = 102 400`).
  Cap initial `make()` capacity; Go slice growth handles
  legitimate-huge naturally.

Decision: **mirror upstream cpp's mechanism in C** — byte-cap +
amortized doubling growth — via compile-time defines for defaults
+ per-allocator vtable fields for fine-grained control. Defaults
(512 KB / 16 MB) tuned against large production schema corpora
rather than upstream cpp's numerical 128 KB, so the doubling-growth
path triggers only on genuine outliers in the primary workload.

Mechanism (item #6 in the in-scope list):

- Allocate `min(claim, cap_bytes / sizeof(element))` upfront.
- When read index reaches current capacity, realloc to
  `min(2 × capacity, claim)`. log₂(claim/cap) reallocs worst case.
- Strings and bytes apply equivalent byte cap.
- Peak allocation bounded by **2 × validated_elements ×
  sizeof(element)**, not by `claim`. Strictly stronger than the
  single-realloc-at-cap shape considered first (which would have
  committed full claim after `cap` validated elements — claim-
  dependent amplification).

Why this shape over the single-realloc-at-cap variant initially
proposed: the doubling shape bounds peak allocation by what the
attacker has actually validated, not by what they claim. A
malicious wire providing K valid elements + 1 garbage forces at
most 2K elements of allocation regardless of the claim. Single-
realloc-at-cap had `max(cap, claim)` amplification on the same
attack, which is bounded but worse.

Why byte cap, not element cap (go-zserio shape): byte cap is
semantically correct — the security concern is *memory
commitment*, which scales with `sizeof(element)`. An element cap
under-protects against large-struct arrays and over-protects
against small-element arrays.

Arena-allocator integration: `zserio_allocator_arena()` returns
caps set to `UINT32_MAX` by default — single-allocation-at-claim
behavior, no doubling-growth arena waste, arena's own `cap`
provides the security bound. This is *cleaner* than upstream
cpp's universal-doubling shape, which would force arena callers
into 2× arena waste at default caps.

## Phasing — status

Each milestone gated the next. Status reflects the tree as of
2026-07.

| Milestone | Deliverable | Status |
| --------- | ----------- | ------ |
| 7.0 | This scope doc accepted. Q1–Q8 resolved. | **Done** — decisions recorded in the log below. |
| 7.1 | Lexer + parser skeleton (primitives + structs only). | **Done.** |
| 7.2 | Full grammar coverage: parser accepts all 182 xvalidation schemas + the upstream `tests/` schemas. | **Done** — acceptance testing lives in `tests/parser/` (`test_acceptance.c`, `test_corpus_compile.c`, `test_corpus_sweep.c`), run via ctest. |
| 7.3 | Semantic analyzer: name resolution, type checking, expression evaluation, packed-array eligibility. | **Done** (`parser/src/sema.c`). |
| 7.4 | Emitter, first cut, for a hand-picked feature-covering schema subset. | **Done.** |
| 7.5 | Full emitter, all 182 xvalidation schemas. | **Done** — the per-commit `xvalidation` CI job runs `zserio_c_gen` as the C-side generator. |
| 7.6 | Decode-hardening emission (in-scope items #5/#6). | **Done as revised** — the pre-allocation guard pair (overflow + remaining-bits) is emitted per wire-driven array; verified by `tests/parser/test_decode_hardening.c` and the decode-path fuzz CI job. The item-#6 allocation-cap design was not adopted (see the as-built note there). |
| 7.7 | Tool-qualification doc + cutover. | **Partially done** — the C build path is fully cut over (a C99 toolchain alone builds `zserio_c_gen`; no JVM on the build path). The Java pieces were *retained*, not removed: `generator/` and the upstream jars remain solely to drive the xvalidation oracle and its sibling gates (perf, MISRA-on-generated-output). The `zserio_c_gen` tool-qualification write-up is an open work item. |

## Future considerations

Items considered during Phase 7 scoping and deliberately deferred,
without committing to a named follow-on phase. Each entry records:
what would be done, why it was deferred, what would trigger
re-evaluation, and whether anything in Phase 7 should be designed
to make the future change cheaper.

### FC-01 — Data/View split for compound types

What: adopt upstream zserio-cpp17's pattern (`doc/Cpp17Design.md:163`
in their repo) — Data struct is a POD owning container, View is a
`const`-only accessor that carries schema-evaluated parameters by
value plus a `const Data *`. Eliminates the `_init`-before-`_read`
shim our current API requires for parameterized types.

Why deferred: API-shape change rippling to every downstream
consumer of our generated headers. Phase 7's xvalidation gate is
wire-level byte-equality, which does not validate API-shape
changes — that correctness would have to come from a separate
integrator dogfooding pass. Bundling two big changes (toolchain
rewrite + API shape) in one cutover makes attribution harder when
something breaks.

Trigger to re-evaluate:

- Concrete integrator pain from the `_init`-before-`_read` pattern
  (silent misinterpretation from wrong parameter, missing init
  call), reported and counted.
- Or: upstream zserio-cpp17 converges enough mind-share that
  Tier-1 integrators expect the Data/View shape and ours becomes
  awkward by comparison.

Phase 7 design hooks: emitter is structured per-compound-kind
(Q3), so retrofitting the Data/View shape later means rewriting
the per-kind emit functions, not the AST / analyzer / parser. Cost
of deferral is bounded to the emitter layer.

### FC-02 — Flat-decode runtime

What: redesign the runtime + codegen so the decoded representation
is itself a single contiguous chunk (capnp / flatbuffers style:
offset-based references instead of pointers, no per-node allocs).
Decoded payload becomes memcpy-able / disk-storable as a single
byte range.

Why deferred: substantial runtime + codegen redesign. Public API
shape changes (views over bytes instead of structs + pointers).
Phase 7 already accepts the tree-of-allocations model.

Trigger to re-evaluate: an integrator requirement for
"decoded payload lives in one contiguous region, memcpy-able,
addressable as one range." Our current arena + tree gives them
*physically contiguous* but not *logically single-chunk*; some
ASIL contexts may require the stronger property.

Phase 7 design hooks: none directly — flat-decode is a runtime
model change, not a parser change. Phase 7 work is independent and
would need to be rewritten on the emitter side for a flat-decode
target.

## Risk register (initial)

| ID | Risk | Mitigation |
| -- | ---- | ---------- |
| R-01 | Hand-written parser drifts from upstream grammar over time. | CI-pinned upstream submodule + 182-schema xvalidation gate + per-release diff of upstream `ZserioParser.g4`. |
| R-02 | Semantic analyzer misses a subtle upstream invariant (e.g. expression context). | Parallel run: every analyzed AST is dumped + diffed against a "Java-front-end structural dump" the existing jar produces. Divergence fails CI. |
| R-03 | Emitter output drifts; xvalidation catches behavior but not source-level stability. | Byte-equal-modulo-whitespace gate during parallel run. Tightens to byte-equal at cutover. |
| R-04 | Effort estimate underruns. The Java front-end has 5+ years of corner cases baked in. | Phasing above is gated, not time-bound. We don't cut over until 7.5's seven-commit criterion is met. Slip is on schedule, not safety. |
| R-05 | Schema-language evolves upstream during Phase 7. | Pin to v2.18.1 for the duration. Upgrade upstream pin only after cutover. |

## Success criteria — status

As scoped, the phase is complete when all of the following hold.
Current status per criterion:

- A C99 toolchain alone produces a working zserio-c generator
  binary. **Met.**
- xvalidation (182 schemas × 5 seeds) is green on every commit.
  **Met** — per-commit CI gate.
- The array-length sanity bound is emitted and verified. **Met** —
  `tests/parser/test_decode_hardening.c` + the decode-path fuzz job.
- The Java jars (`zserio_core.jar`, `generator/zserio_c.jar`) are
  off the C build path. **Met as revised** — they are retained in
  the tree solely as the xvalidation-oracle driver and for the
  gates that feed off it, not removed outright as first scoped.
- Generator-output MISRA gate green. **Met with a caveat** — the
  gate (`tools/run_misra_generated.sh`) currently generates its
  input through the retained Java extension pipeline; pointing it
  at `zserio_c_gen` output is an open follow-up.
- The parser carries its own coverage, fuzz, and
  MISRA-or-justified-deviation evidence in the safety case, and a
  `tool-qualification-parser.md` is accepted by independent review.
  **Open** — the parser has fuzz coverage via the decode-path fuzz
  job and is held to the strict C99 `-Werror` warning gate, but the
  dedicated qualification write-up and a MISRA gate over
  `parser/src/` are outstanding.

## Decisions log

(Each entry: question id, decision, date, rationale. Recorded as
resolved at scoping time; as-built divergences are noted inline.)

| Q | Decision | Date | Rationale |
| - | -------- | ---- | --------- |
| Q1 | Hand-written recursive descent + Pratt for expressions. | 2026-05-15 | Minimum TQ-2 surface (no third-party parser-toolkit dependency); full control over diagnostics + error recovery; single C99 toolchain preserves Phase 7's build-hermeticity goal. Grammar size (~500 LOC of `.g4`) is well inside hand-written territory. Upstream grammar change rate confirmed mature: ~2–3 substantive changes per year, decelerating to zero (20 months with no changes leading into v2.18.1). Drift risk small enough that the 182-schema xvalidation harness + a documented per-release diff procedure is sufficient. |
| Q2 | Tagged union per node kind, arena-allocated. ~30–50 distinct kinds collapsed from upstream's 105 Java classes. | 2026-05-15 | MISRA-cleanest of the three options (no casts, no vtables, no flex arrays); memory-cheapest (single struct size, arena-allocated, zero per-node alloc overhead); visitor-cleanest (`switch(kind)` with `default: assert(0)` catches missing handlers at compile time when the enum grows). Production precedent: lua, tcc, chibicc, Zig stage1. The "every switch needs updating when kinds grow" property is the safety property we want. AST lives in a per-compilation arena; lifetime matches parser+analyzer+emitter exactly. |
| Q3 | Hand-written tree-walking print pass via a small disciplined emitter API (`emit_lit`, `emit_indent`, `emit_block_open/close`, `emit_fmt`). One emit function per concern (~50–80 functions, est. 5–10 kLOC). No template engine. | 2026-05-15 | Single C99 toolchain end-to-end (option C would have kept FreeMarker + JVM in the build, halving Phase 7's hermeticity goal); MISRA-coverable like the rest of the project's C; debuggable in `gdb`; cross-cutting emission patterns become reusable helpers the compiler enforces. Embedded template-engine (option B) was rejected because it reintroduces a templating-engine surface (Rule 21.6 framing + multi-layer indirection) without buying much given FreeMarker's heaviest use in our templates is conditionals + loops which don't compress into template strings anyway. Per-schema xvalidation-byte-equal-at-the-wire (not source-equal-to-Java-generator) is the correctness gate. |
| Q4 | Parallel run with byte-equal-at-the-wire cutover. Both generators build in CI for the duration; the C generator's xvalidation gate must be green for 7 consecutive main commits before cutover (matches milestone 7.5). Source-level byte-equality is **not** required — wire-level byte-equality is the gate. Java generator is **retained** after cutover (not deleted; as built it stays in place at `generator/`, serving as the xvalidation-oracle driver), but **excluded from the safety case** at cutover. Rationale for retention: (1) integrators need to compile a dozen custom schemas through both pipelines side-by-side during their own cutover ramp; (2) a C emitter sitting alongside cpp/java generators may stay useful long-term as one of several generator outputs; (3) keeps an option open to extract the retained tree to a separate repo at a future major-version boundary without scrambling. Excluded-from-safety-case framing: the Java generator's qualification ends at cutover; any post-cutover use is at the integrator's risk under their own QA. C-generator CI can drop from per-commit to nightly for milestones 7.1–7.4, re-entering per-commit gating at 7.5. | 2026-05-15 | Lowest regression risk (every commit runs both pipelines through xvalidation; divergence fails build); cleanest safety-case transition (gradual overlap is what ISO 26262-8 calls for); reversible at every step; aligns with the "N consecutive green commits" cutover pattern used in Phase 5/6. The legacy-retention choice (vs. delete) is a deliberate trade: small ongoing maintenance burden on a frozen tree in exchange for integrator-side cutover flexibility and the option to repurpose later. |
| Q5 | MISRA C:2012 strict on parser + analyzer + emitter, same gate as the runtime. One up-front documented deviation: **Rule 17.2 (no recursive functions)** for the recursive-descent parser, bounded by the grammar's static maximum nesting depth and closed by the existing stack-budget gate (REQ-FS-003). All other rule frictions handled by workaround, not deviation: typed `emit_lit`/`emit_ident`/`emit_block_open` helpers instead of `emit_fmt` (Rule 17.1, `<stdarg.h>`); allocator-vtable interning instead of `strdup` (Rule 21.x); open-addressing hash tables; arena-allocated AST. Policy for unanticipated frictions: **prefer adding a bounded, documented deviation over falling back to permissive C or to a Java half-step.** Each new deviation lands as a sibling under `docs/misra-deviations/` with the same evidence shape as D001–D006 (rule, scope, justification, residual-risk argument, sibling cppcheck-suppressions cross-ref). | 2026-05-15 | Captures the safety value of doing the Phase 7 rewrite at all (option B would have left the safety story essentially unchanged from today's Java front-end + qualified-by-validation model — see Q5 discussion 2026-05-15). MISRA + validation stack rather than substitute; option A gives TCL2/3-shaped confidence at TCL1 effort. Effort tax estimated at +20–30% over a permissive C parser; absorbable in Phase 7's overall scope. Removes the "which TU is under which standard" asymmetry. Aligns the source-level standard with the runtime, simplifying independent-reviewer briefing. The Rule 17.2 deviation is in-pattern (recursive-descent recursion is a well-precedented deviation in production safety-critical compilers). **As built:** the coding discipline is followed in `parser/src/`, but the CI MISRA gate does not yet cover the parser sources — open item, see the Q5 body note. |
| Q6 | `(file_id, source_offset)` pair per AST node. Lex tracks only file+offset (no per-character line counter in the hot path). Line/col recovered on demand from a lazily-built per-file line-start table; first diagnostic on a file builds the table, subsequent diagnostics reuse it. Diagnostics default to gcc-style human-readable (`schema.zs:42:18: error: ...` with carat-pointed source snippet); optional `--diagnostic-format=json` for tooling consumers. **Multi-error reporting capped at 20 errors per compile**, matching gcc/clang behavior; analyzer passes set a per-site "already-recorded" flag to suppress cascading nonsense. | 2026-05-15 | Smallest cost in the lexer hot path (no per-character line-counter); compact node shape (6 bytes, 8 after padding); pay-on-use diagnostic cost; gcc-style format is universally tooled-for in editors/IDEs; multi-error is the production-compiler norm and the right UX for schema refactors. The 20-error cap stops error cascades from drowning the useful signal. |
| Q7 | Defer the Data/View split. Tracked under "Future considerations" FC-01, not committed to a named follow-on phase. Phase 7 emits the current API shape (parameters as struct members + `_init`-before-`_read` shim). Re-evaluation triggered only by concrete integrator pain or upstream convergence pressure. | 2026-05-15 | Phase 7 scope already large (parser + analyzer + emitter + MISRA-on-toolchain + xvalidation pipeline + safety case update). The wire-level byte-equality cutover gate (Q4) doesn't validate API-shape changes; bundling toolchain rewrite + API reshape in one cutover makes attribution harder. Cost-of-deferral is bounded: Q3's per-compound-kind emitter structure means a future retrofit touches the emitter only, not the parser/analyzer. Future-considerations list (rather than Phase 8 commitment) avoids speculative roadmap. |
| Q8 | Mirror upstream cpp v2.18.1 patch `16f1b8c34` mechanism in C: **byte-cap + amortized doubling realloc growth**, configured via (a) compile-time defines `ZSERIO_DEFAULT_MAX_INITIAL_ARRAY_BYTES` (default **512 KB**) and `ZSERIO_DEFAULT_MAX_INITIAL_BLOB_BYTES` (default **16 MB**), overridable at integrator's build via `-D`; (b) per-allocator vtable fields `max_initial_array_bytes` / `max_initial_blob_bytes` (sentinel 0 = use compile-time default, `UINT32_MAX` = no cap). `zserio_allocator_libc()` returns the compile-time defaults; **`zserio_allocator_arena()` returns `UINT32_MAX` / `UINT32_MAX`** — arena's own `cap` is the security bound, no doubling-growth arena waste. Generated reader emits `realloc(min(2×capacity, claim))` on grow. Peak allocation bounded by 2× actually-validated elements (strictly stronger than the single-realloc-at-cap variant first considered, which had claim-dependent amplification). Defense-in-depth on top of Phase 7 item #5; together they close both attack surfaces of the upstream unbounded-allocation advisory. Lands as scope item #6; toolchain qualification renumbered to #7. ABI change to `zserio_allocator_t` lands at Phase 7 cutover. **As built: not adopted** — see the as-built note on in-scope item #6; `zserio_allocator_t` kept its two-field shape, no cap requirement was added to the safety requirements (they end at REQ-FS-008), and the shipped defense is the item-#5 guard pair. | 2026-05-15 | The unbounded-memory-allocation-in-deserialization advisory (patched upstream in v2.18.1) documents a concrete vulnerability across the upstream zserio family. Upstream cpp v2.18.1 fix shape (byte-cap + doubling) is the correct industry-aligned mechanism: byte-cap is semantically correct (peak memory depends on `sizeof(element)`); doubling growth bounds peak allocation by *validated* elements × 2, not by `claim`. Single-realloc-at-cap variant initially considered had claim-dependent amplification — worse property, rejected. Compile-time defaults preferred over `_libc_strict()` constructor variant because they don't add API surface and are overridable per-build (cleaner than go-zserio's env-var-at-package-init, which doesn't fit a C library without `init()` functions). Arena-allocator defaults to `UINT32_MAX` because the arena's own `cap` is already the security bound — gives us a cleaner ASIL story than upstream cpp's universal-doubling shape, which forces 2× arena waste. Numerical defaults (512 KB / 16 MB) tuned per workload observations on large production schema corpora (3–6K-element arrays, 5 MB+ blob outliers); paranoid build overrides toward upstream-cpp-equivalent 128 KB via `-D` flags. |
