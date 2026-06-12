---
id: REQ-FN-007
title: Generator output is deterministic across runs
type: functional
status: accepted
verification: test
---

## Statement

Given identical inputs, the generator SHALL produce byte-identical
outputs across:

1. Distinct invocations on the same host with the same generator
   binary.
2. Distinct hosts running the same `zserio-c` version (different
   operating systems, CPUs, working directories).
3. Distinct compilers and optimization levels applied when
   building the generator.

"Identical inputs" means the same set of schema files, the same
ordered list of `-src` roots, the same `-c` output directory
flag, and the same `-setTopLevelPackage` argument.

"Byte-identical outputs" means: for every emitted file, the
byte contents and the file path are identical; the set of
emitted file paths is identical; no additional or absent files.

The generator SHALL NOT embed timestamps, build identifiers,
hostname, working directory, environment variable contents, or
random tokens in its output.

## Rationale

Deterministic generator output is a precondition for build
reproducibility, content-addressed caches, supply-chain
attestation (e.g. SLSA), and the safety-case argument that the
generated code is a function of the schema alone.

It also makes regression testing tractable: a CI diff against a
golden-output reference is meaningful only if the comparison is
not noisy.

The prohibited inclusions (timestamps, hostnames, etc.) are the
specific accidental-non-determinism sources that creep in via
"helpful" header comments. The requirement is phrased
negatively so that any future contributor adding a `// Generated
on <date>` line trips the gate.

## Verification

- `tests/parser/test_generator_determinism.c` runs the
  generator twice on the same input and `diff`s the entire
  output tree.
- A CI matrix builds the generator with gcc and clang on
  x86_64-linux and aarch64-linux, runs each binary against the
  same schema corpus, and `diff`s the resulting trees. Any
  byte difference fails the build.
- The `tests/xvalidation/` golden-output cache depends on this
  property: a second run on the same schema must produce the
  same bytes.

## Notes

Discharges Safety Goal SG-7 at the generator layer (the runtime
layer is discharged by REQ-DM-003).

Implementation note: the generator uses a stable
package-source-order ordering for output files and a stable
alphabetical ordering for emitted symbols. Iteration order of
internal hashmaps is normalized before emission. Changes that
introduce a new hashmap dependency on the emission path require
sorting before output.
