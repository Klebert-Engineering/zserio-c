---
id: REQ-DM-006
title: Schema evolution constructs
type: domain
status: accepted
phase_target: 1
verification: review
---

## Statement

The schema language SHALL provide explicit, named language
constructs that permit a schema author to evolve a schema while
preserving defined compatibility properties with previously
deployed encoders and decoders. The constructs SHALL include, at
minimum:

1. `optional T name;` — an on-wire 1-bit presence tag plus
   optional value, supporting both forward and backward
   compatibility when adding a field.
2. `T name if cond;` — a value conditionally encoded based on
   the runtime value of a previously decoded field, supporting
   conditional-presence semantics.
3. `extend T name;` — a value encoded at the end of a top-level
   structure, decoded only if the bitstream has bytes remaining,
   supporting backward-compatible top-level extension.
4. `@removed` annotation on enumeration items, supporting
   forward-compatible removal of an enumerator without
   disturbing the wire encoding of remaining items.

The behavior of each construct SHALL be normatively specified in
`docs/spec/schema-evolution.md` and SHALL be sufficient to
characterise the compatibility property the schema author is
opting into.

The wire format SHALL NOT include any per-field tag or
self-description that would allow schema-evolution semantics
beyond what these explicit constructs support. In particular,
adding or removing a non-`optional` non-`extend` field SHALL be
treated as a wire-format breaking change.

## Rationale

Schema evolution is the property by which a deployed system can
upgrade one end of a serialization channel without simultaneously
upgrading the other. In a long-lived automotive system, this is
an operational requirement: ECUs across a fleet do not update
atomically.

Zserio chose an *opt-in* evolution model: the schema author
explicitly declares which fields can evolve, and the wire format
is otherwise tight (no tags, no length-prefixes, no
self-description). This is a deliberate trade-off versus
protobuf's tagged-by-default model — tighter encoding in exchange
for less elasticity.

The requirement is normative because the alternative would be to
let evolution happen by accident: a schema author renames a
field, the new encoder produces a different bit layout than the
old decoder expects, and the system silently misinterprets
bytes. By naming the four explicit constructs and forbidding
implicit evolution, we make every wire-format-breaking change
visible at schema-review time.

## Verification

Reviewed against `docs/spec/schema-evolution.md` at every change
to the parser or codegen that affects how `optional`, `if`,
`extend`, or `@removed` are processed.

The compatibility properties of each construct are exercised by
harnesses under `tests/xvalidation/harnesses/`:

- `auto_optional`, `auto_optional_bool` — `optional`
- (various `*_if_*` harnesses) — conditional
- `extend_struct`, `extend_array_field` — `extend`
- `enum_with_removed_item_param_choice` — `@removed`

## Notes

This requirement constrains the schema language, not the
runtime. The runtime's job is to implement whatever the schema
language defines (see [REQ-FN-025](../functional/REQ-FN-025.md)).

The decision *not* to support implicit evolution is part of the
safety case argument: pinned schema versions are how integrators
prove interoperability. See
[`docs/safety-case/hazard-analysis.md`](../../docs/safety-case/hazard-analysis.md)
H-05.

Schema evolution is distinct from *versioning of the language
itself* (REQ-DM-007). One is about evolving a particular schema;
the other is about evolving the schema language across releases.
