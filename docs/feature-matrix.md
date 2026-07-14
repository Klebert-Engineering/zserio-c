# Feature-support matrix

**Baseline:** upstream zserio v2.18.1 (`third_party/zserio/`).

**Status:** informative. The normative language reference is
[`spec/schema-language.md`](spec/schema-language.md); this page is the
quick answer to "will my schemas work with zserio-c?"

zserio-c is wire-format compatible with upstream zserio: a schema that
restricts itself to the supported features encodes and decodes
byte-identically here and against upstream v2.18.1. Each feature below
carries one of four statuses:

- **Full** — parsed, analyzed, and generated; verified by the
  cross-validation harness ([`tests/xvalidation/`](../tests/xvalidation/)),
  which runs 183 schemas × 5 seeds on every commit and requires the C
  and upstream-C++ wire output to be byte-equal in both directions.
  The Notes column cites representative harness names.
- **Partial** — accepted, but with limited semantics as noted.
- **Rejected** — recognized by the parser but rejected during semantic
  analysis with a compile-time diagnostic; generation fails. Nothing
  is ever silently mis-generated.
- **Out of scope** — intentionally unsupported by design; the reason
  is given.

## Built-in types

| Feature | Status | Notes |
|---|---|---|
| Fixed-width integers `int8`–`int64`, `uint8`–`uint64` | Full | `all_builtin_types`, `field_type` |
| Bit fields `bit:N` / `int:N`, literal widths | Full | `bit5_indexed_offset_array`, `int14_indexed_offset_array` |
| Bit fields with dynamic widths `bit<expr>` / `int<expr>` | Full | `dynamic_bitfield_param`, `bitfield_uint64_length`, `dynamic_bitfield_length_bounds` |
| Variable-length integers `varint16`–`varint64`, `varuint16`–`varuint64`, `varint`, `varuint` | Full | `all_builtin_types`, `varint32_indexed_offset_array` |
| `varsize` | Full | `all_builtin_types` |
| `float16`, `float32`, `float64` | Full | `float_type` |
| `bool` | Full | `all_builtin_types`, `bool_param_choice` |
| `string` | Full | `structure_string`, `one_string_structure` |
| `bytes` | Full | `structure_bytes`, `bytes_param` |
| `extern` | Full | `structure_extern`, `extern_param` |

## Declarations

| Feature | Status | Notes |
|---|---|---|
| Packages and imports | Partial | Imports are parsed and used to locate schema files, but visibility is not gated: all top-level declarations share one namespace, disambiguated by the package suffix in generated identifiers. Stricter import-gated visibility is planned ([#10](https://github.com/Klebert-Engineering/zserio-c/issues/10)). |
| Constants | Full | All value kinds, including negative, boolean, floating-point, and string constants; `const_type`, `full_const_type`, `constant_in_choice_case` |
| Subtypes | Full | `structure_subtype`, `uint16_subtype`, `subtype_imported` |
| Enumerations | Full | `enumeration_type`, `full_enumeration_type` |
| Bitmasks | Full | `bitmask_type`, `full_bitmask_param_choice` |
| Structures | Full | `simple_structure`, `empty_structure`, `structure_value` |
| Unions | Full | `simple_union`, `union_with_parameter`, `union_with_multiple_parameters` |
| Choices (multi-label cases, `default`, empty arms) | Full | `expression_selector_choice`, `bool_choice_with_default`, `default_empty_choice` |
| `sql_table` / `sql_database` | Rejected | Diagnostic: "sql_table is not supported; SQLite-backed generation is planned as an optional extension outside the ASIL scope" (and likewise for `sql_database`). Tracked in [#8](https://github.com/Klebert-Engineering/zserio-c/issues/8). |

## Fields, arrays, and layout

| Feature | Status | Notes |
|---|---|---|
| Auto optional (`optional` keyword) | Full | `auto_optional`, `structure_optional`, `auto_optional_bool` |
| Conditional optional (`if` clause) | Full | `optional_expression`, `optional_complex_expression` |
| `optional` combined with `if` on one field | Rejected | Diagnostic: "a field cannot combine 'optional' with an 'if' condition (upstream zserio rejects this too)". |
| Constraints | Full | `structure_constraints`, `choice_constraints`, `union_constraints`, `literal_constraint` |
| Default values | Full | `structure_default_values`, `optional_default_values`, `extended_default_values` |
| Extended fields (`extend`, forward compatibility) | Full | `extended_simple_field`, `multiple_extended_fields`, `extended_packed_array` |
| Functions in structures and unions | Full | `function_type`, `bitfield_function_length`, `function_selector_choice` |
| Functions in choice bodies | Rejected | Diagnostic: "functions in choice types are not yet supported". Support is planned ([#13](https://github.com/Klebert-Engineering/zserio-c/issues/13)). |
| Fixed-length arrays | Full | `fixed_array_uint8`, `array_type` |
| Variable-length arrays | Full | `variable_array_subtyped_struct`, `structure_array` |
| Auto arrays | Full | `auto_array_uint8`, `auto_array_struct_recursion` |
| Packed arrays (all element shapes, incl. nested compound delta packing) | Full | `packed_arrays_mapping`, `packed_auto_array_struct_recursion`, `packed_auto_array_struct_with_unpacked_field` |
| Implicit arrays (`implicit` keyword) | Rejected | Diagnostic: "implicit arrays are not supported (deprecated upstream); use an auto array 'T x[]' or an explicit length". |
| `align(N)` | Full | `bit_alignment`, `optional_member_alignment` |
| Scalar offsets (`expr: field`) | Full | `bit_offset`, `nested_offset`, `uint64_offset`, `parameter_offset` |
| Indexed offsets (`field[@index]:`) | Full | `auto_indexed_offset_array`, `compound_indexed_offset_array`, `packed_auto_indexed_offset_array` |

## Expressions

| Feature | Status | Notes |
|---|---|---|
| Arithmetic, bitwise, shift, comparison, logical operators | Full | `modulo_operator`, `left_shift_operator`, `negation_operator`, `parenthesis` |
| Conditional operator `?:` | Full | `question_mark`, `variable_array_ternary_operator` |
| Member access and array indexing | Full | `access_within_type`, `index_operator`, `structure_parent_child_value` |
| `lengthof`, `valueof`, `numbits`, `isset`, `@index` | Full | `lengthof_operator`, `valueof_operator`, `numbits_operator`, `isset_operator`, `index` |
| Casts and integer promotion | Full | `cast_uint8_to_uint64` |

## Generics

| Feature | Status | Notes |
|---|---|---|
| Parameterized types | Full | `simple_param`, `multiple_params`, `grand_child_param`, `parameterized_nested_in_array` |
| Templates | Full | `templated_choice`, `choice_templated_field`, `constraints_in_templates`, `expression_full_template_argument` |
| `instantiate` | Full | `instantiate_simple`, `instantiate_nested_template`, `instantiate_type_as_parameter` |

## Services and runtime extensions

| Feature | Status | Notes |
|---|---|---|
| `service` | Full | Transport-agnostic C interfaces: typed client wrappers and a server dispatch over the `zserio_service_t` vtable; payloads are plain zserio wire bytes, so a zserio-c peer interoperates with an upstream peer. Verified by `tests/parser/service_pubsub_roundtrip.c` (services carry no wire payload of their own, so there is no xvalidation harness). |
| `pubsub` | Full | Typed `publish`/`subscribe` wrappers over the `zserio_pubsub_t` vtable. Topics must be string literals; a non-literal topic fails generation with "pubsub topic must be a string literal". Verified by `tests/parser/service_pubsub_roundtrip.c`. |
| JSON debug encode/decode | Out of scope | Upstream's JSON dump/load relies on runtime type information, which zserio-c does not generate. |
| Reflection, type-info, walker | Out of scope | [REQ-FS-008](../requirements/safety/REQ-FS-008.md) forbids runtime type-information structures in generated code; all type knowledge is compiled into direct C calls. |
| Multi-language code generation | Out of scope | zserio-c generates C99 only; use upstream zserio for other targets. |
| SQLite runtime layer | Out of scope | Planned as a separate, opt-in component outside the ASIL scope; see the `sql_table` / `sql_database` rejection above and [#8](https://github.com/Klebert-Engineering/zserio-c/issues/8). |

## Metadata and directives

| Feature | Status | Notes |
|---|---|---|
| Documentation comments (`/** */` and `/*! !*/`) | Partial | Both forms are accepted for upstream compatibility, but their content is discarded after parse — not propagated to generated code. Propagation into generated headers is planned ([#9](https://github.com/Klebert-Engineering/zserio-c/issues/9)). |
| `@deprecated` annotation | Partial | Parsed and recorded; no effect on generated code yet (no deprecation attribute or warning). Tracked in [#11](https://github.com/Klebert-Engineering/zserio-c/issues/11). |
| `@removed` annotation | Partial | Parsed and recorded; the item is still emitted and writable, but its numeric value (and those of subsequent items) is preserved, which is the property wire compatibility depends on. Exercised by `structure_with_removed_enum_item_field`, `packed_auto_array_removed_enum_item`. Write-time rejection of removed items is tracked in [#11](https://github.com/Klebert-Engineering/zserio-c/issues/11). |
| `zserio_compatibility_version` | Partial | Parsed and recorded; no feature-availability check is performed against the declared version (planned, [#12](https://github.com/Klebert-Engineering/zserio-c/issues/12)). Upstream enforces it. |
| `rule` / `rule_group` | Partial | Parsed; the generator emits a comment-only marker header and nothing else. Rules have no wire or runtime effect upstream either. |

## Migrating from upstream zserio

The differences an upstream user encounters first:

- **Rejected constructs fail loudly.** Every construct in a Rejected
  row above produces a compile-time diagnostic and a nonzero generator
  exit code. There is no silent divergence: if `zserio_c_gen` accepts
  a schema, the wire format matches upstream.
- **`sql_table` / `sql_database` must be split out.** Schemas that mix
  SQL declarations with plain types need the SQL parts moved to a
  separate package (or removed) before the rest generates.
- **`implicit` arrays must be rewritten** as auto arrays (`T x[]`) or
  arrays with an explicit length. Upstream deprecated implicit arrays;
  zserio-c rejects them outright.
- **Doc comments are not propagated** to generated headers yet; schema
  documentation tooling that reads generated code will not see them.
- **No JSON debug I/O and no reflection.** Debug dumps and generic
  walkers must be replaced by ordinary generated accessors.
- **The generated API is C, not C++ classes.** Every fallible function
  returns `zserio_status_t`, output flows through pointer parameters,
  and all allocation goes through a caller-provided
  `zserio_allocator_t` vtable — there are no exceptions, no implicit
  heap, and no thread-safety guarantees (one reader/writer per
  thread).
- **Imports are not visibility-enforcing yet.** A schema that
  references a type it never imports is accepted today; portable
  schemas should still declare their imports, since stricter
  visibility is planned.

## Verification

The statuses above are enforced by CI, not asserted by hand:

- **Full** rows are gated by the cross-validation harness
  ([`tests/xvalidation/`](../tests/xvalidation/)): 183 schemas × 5
  seeds, byte-equal against upstream C++ in both directions on every
  commit. Harness names cited in the Notes columns correspond to
  directories under
  [`tests/xvalidation/harnesses/`](../tests/xvalidation/harnesses/).
- **Rejected** rows are gated by semantic-analysis tests in
  [`tests/parser/test_sema.c`](../tests/parser/test_sema.c)
  (`test_implicit_array_is_rejected`,
  `test_optional_with_if_is_rejected`,
  `test_sql_declarations_are_rejected`,
  `test_choice_function_is_rejected`).
- **Partial** rows state exactly what is and is not implemented; the
  corresponding sections of
  [`spec/schema-language.md`](spec/schema-language.md) carry the
  normative implementation-status notes.
