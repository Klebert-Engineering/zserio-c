# Upstream analysis

This directory captures what we learn from studying upstream `ndsev/zserio`
(at the pinned version `v2.18.1`, vendored under `third_party/zserio/`).
The purpose is to inform design decisions in this port; we are not
documenting upstream for its own sake.

## Topics to cover (Phase 0 work)

- **Schema language surface**: the full set of constructs we must support
  (structs, choices, unions, enums, bitmasks, optionals, parameterized
  types, constraints, services, pubsub, sql tables/databases…). For each,
  whether it is in scope for v1, deferred, or out of scope.
- **Wire format**: bit layout for varints, alignment rules, byte order,
  bool encoding, string encoding, optional/array headers.
- **C++ generator**: how it is structured (templates, AST visitor, output
  modules) — useful as a reference for the Java extension we will write.
- **C++ runtime hot paths**: where time is spent in encode/decode; we want
  to beat these, so we need to know what they are.
- **ASIL pain points**: concrete reasons the C++ runtime is not viable for
  ISO 26262 production (heap usage in containers, exceptions on bad input,
  RTTI, STL allocator surface, etc.).
- **Test corpus structure**: how upstream tests are organized so we can
  reuse them in our conformance suite (Phase 4).
- **Plugin SPI**: the Java extension API we will target in Phase 2.
  What the AST looks like, what hooks exist, what FreeMarker templates
  upstream uses.

## Convention

- One markdown file per topic, named for the topic (e.g. `schema-language.md`,
  `wire-format.md`).
- Each file ends with an "Implications for zserio-c" section that names
  the design choices it forces or strongly suggests.
- When a topic results in a new requirement, link the requirement ID.
- When upstream code is referenced, cite the path inside `third_party/zserio/`
  and the `v2.18.1` tag.
