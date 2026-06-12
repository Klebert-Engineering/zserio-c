---
id: REQ-FN-013
title: Template instantiation
type: functional
status: accepted
verification: test
---

## Statement

For every `instantiate` declaration in a parsed schema set, the
generator SHALL emit a concrete C type corresponding to the
template parameters substituted into the template definition. The
emitted concrete type SHALL:

1. Be addressable by an identifier derived from the
   instantiation site's name (or, where unnamed, from the
   template name with the parameter list appended), per the
   naming rules of [REQ-FN-009](REQ-FN-009.md).
2. Have the same lifecycle entry points (`_init`,
   `_set_parameters`, `_read`, `_write`, `_free`) as a
   non-templated type of equivalent shape, per
   [REQ-FN-015](REQ-FN-015.md).
3. Be wire-compatible with the equivalent non-templated type
   defined directly, per [REQ-FN-001](REQ-FN-001.md).

The generator SHALL support template parameters that are:

1. Other types (including other templated types — multi-level
   instantiation).
2. Schema-language constants (integer-valued expressions).
3. Enum members and bitmask members.

The generator SHALL emit template definitions themselves NO code:
only concrete instantiations produce output. A template that is
declared but never instantiated produces no generated files.

Multi-level template chains (template-of-template-of-...) SHALL
be supported to the depth reached by realistic schemas; the NDS
Live conformance corpus provides the current depth target.

## Rationale

Templates are the schema language's mechanism for generic
container types and parameterized compound types. Without
template support, large fractions of practical zserio schemas
(notably NDS Live's attribute hierarchy) cannot be processed.

The "wire-compatible with equivalent non-templated definition"
clause is what makes template instantiation a *front-end* feature
of the generator: by the time wire-format decisions are made,
the template has been resolved and the AST looks identical to a
non-templated equivalent.

The "no code for definitions" clause is a hygiene rule: emitting
unused template definitions would balloon the generated tree
and would force the runtime to handle generic types it never
sees in instances.

Multi-level depth support is the requirement-side reason the
identifier-length cap ([REQ-FN-010](REQ-FN-010.md)) exists:
deep chains produce long names, and the truncation rule is what
keeps them filesystem-valid.

## Verification

- The cross-validation harness includes 28 template-related
  harnesses (`choice_templated_*`, `default_values_in_templates`,
  `constraints_in_templates`, `expression_*_template_argument`,
  …) covering scalar / enum / bitmask / type template
  parameters and multi-level chains.
- The NDS Live conformance corpus exercises ~600 templated types
  across ~140 schemas. All instantiate, generate, and run under
  the round-trip read_tile gate.
- `tests/parser/test_template_unused.c` exercises an unused
  template definition and verifies no output.

## Notes

The implementation lowers `instantiate Foo<X> Bar;` to an AST
node that names the parameter substitution; semantic analysis
then walks the template definition under the substitution and
produces a concrete type AST equivalent to a hand-written
`Bar`. From the emitter's perspective, templates do not exist
after sema.

This is one of the most surface-area-heavy features of the
generator; the volume of cross-validation harnesses covering it
is the result.
