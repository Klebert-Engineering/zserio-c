---
id: REQ-FS-008
title: No runtime reflection in generated code
type: safety
status: accepted
phase_target: 2
verification: analysis
---

## Statement

Code emitted by the zserio-c generator SHALL NOT, by default, contain
runtime type-information data structures (analogous to upstream
`ITypeInfo` / `IReflectable`), and SHALL NOT contain virtual dispatch
tables, type-name string tables, or any other artifact whose purpose is
to enable type introspection at runtime. All knowledge about a schema's
type structure SHALL be encoded as direct C function calls and field
accesses produced from the schema at code-generation time.

A future generator option MAY add an opt-in reflection emission mode
for diagnostic or interoperability use. Such a mode SHALL be off by
default and SHALL be excluded from any safety-relevant build.

## Rationale

Reflection is fundamentally a runtime type-walking mechanism for code
that does not know its types statically. A codec generated from a fixed
schema does know its types statically: the encode/decode of every field
is fully determined at code-generation time and can be emitted as
direct, inlinable C. Carrying reflection metadata into the binary buys
nothing on the codec's hot path and has substantial costs:

- **Code size** — type-info tables and the strings naming members can
  add tens of bytes per type. Across a real schema (hundreds of types)
  this is non-trivial flash usage on small ECUs (REQ-PF-002).
- **Certification surface** — every byte of read-only data is part of
  the safety case argument. Type strings are inert but still need to be
  accounted for. Vtables are worse: indirect calls defeat MISRA Rule
  19.x category analyses and complicate WCET bounding.
- **API surface** — reflection encourages "generic walker" APIs that
  invite caller code to peek at internals. A reflection-free generated
  type is opaque except through the encode/decode entry points, which
  is exactly the boundary we want for REQ-FS-006 input validation.

Upstream emits reflection because the C++ runtime's JSON, debug-string,
and walk/validation features depend on it. zserio-c does not need those
features in safety-relevant builds; where they are needed (e.g. a JSON
encoder, see `01-schema-language.md`), they are provided by per-type
generated code, not by a runtime type walker.

## Verification

- A generator unit test compiles a representative schema and asserts
  that the emitted `.c`/`.h` contain no symbols matching
  `*_typeInfo`, `*_reflectable`, `*_typeName`, `vtable_*`, no virtual
  function table layout, and no string literals naming schema types
  or members.
- A `nm`-based check on the compiled object asserts that no
  exported data symbols carry type metadata.

## Notes

- Optional fields, unions, and choices are still implementable
  reflection-free: the generator emits explicit `union` types tagged
  by an enum, with per-tag encode/decode functions selected by a
  switch in generated code.
- Equality, copy, and free helpers for compound types are emitted
  per-type as direct C functions, not driven by reflection.
- This requirement does not forbid the generator from producing
  *separate* artifacts that *describe* a schema (e.g. a JSON schema
  document, a header of constants giving field offsets). It forbids
  embedding such descriptions into the runtime-loadable code.
