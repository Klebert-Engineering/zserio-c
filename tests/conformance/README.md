# `tests/conformance/`

Conformance metadata shared by the cross-validation harnesses.
The actual byte-equal cross-validation lives in
`tests/xvalidation/` (182 schemas × 5 seeds per CI run);
supplementary end-to-end conformance against a production
NDS.Live payload is maintained in a private companion repository
(NDS Association membership required).

## Files

- **`error_mapping.csv`** — maps each upstream zserio C++ throw site
  (file:line) to the corresponding `zserio_status_t` value our runtime
  returns for the same error. Referenced by
  [Error handling § Mapping to upstream zserio C++](../../docs/user-guide/error-handling.md)
  for anyone porting a C++ caller to the C bindings. Maintained
  alongside the `zserio_status_t` enum: any new status code or any
  new upstream throw-site mapping goes through this file.
