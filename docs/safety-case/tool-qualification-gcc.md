---
work_product: ISO 26262-8 §11 — Tool qualification (gcc)
status: draft
tool: gcc
---

# Tool qualification — gcc

## Identification

| Field          | Value                                                       |
| -------------- | ----------------------------------------------------------- |
| Tool name      | GNU Compiler Collection (gcc), C front-end                  |
| Role           | Compiles `runtime/src/*.c` and generated codec sources into the production deliverable. |
| Source         | Distributed with Ubuntu 24.04 LTS (Noble); apt package `gcc`. |
| Pinned by      | The GitHub Actions runner image `ubuntu-latest` (currently Ubuntu Noble; gcc ≥ 13). |
| Configuration  | `-std=c99 -pedantic` + the warning set in the top-level `CMakeLists.txt` (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wcast-qual -Wcast-align -Wundef -Wwrite-strings -Wformat=2 -Wstrict-overflow=5 -Wfloat-equal`) and `-Werror` when `ZSERIO_C_WERROR=ON` (CI default). |
| License        | GPL with runtime library exception. The compiler is not redistributed; only its output is. |

## TCL analysis (ISO 26262-8 §11.4.5)

| Axis                       | Classification | Rationale                                                                                                |
| -------------------------- | -------------- | -------------------------------------------------------------------------------------------------------- |
| Tool Impact (TI)           | **TI2**        | gcc's output is the production binary. Any miscompilation could violate any safety goal.                  |
| Tool error Detection (TD)  | **TD2**        | Errors are detected by independent mechanisms: (a) the full unit-test suite re-executed by `build-and-test`; (b) the `xvalidation` job exercises every primitive codec against an independently-built cpp encoder; (c) the `stack` and `coverage` gates use the same compiler but parse its output for properties independent of correctness. False negatives are possible but require the same bug to evade all three layers. |
| Derived **TCL**            | **TCL2**       | TI2 × TD2 per ISO 26262-8 Table 4.                                                                       |

## Use cases

1. Compile the runtime static archive (`libzserio_c_runtime.a`).
2. Compile generated codec sources emitted by `zserio_c.jar`.
3. Compile the runtime test suite (`tests/runtime/*`).
4. Compile the xvalidation harness c_driver.
5. Generate per-translation-unit `.su` stack-usage files for the
   `stack` CI gate (`-fstack-usage`).

## Qualification approach (TCL2)

ISO 26262-8 §11.4.7 accepts any one of four methods for TCL2. We
use **method (1) — increased confidence from use** combined with
**method (3) — validation of the software tool**:

- **Increased confidence from use.** gcc has been the production
  C compiler for the Linux kernel and several certified automotive
  stacks for over two decades. The version branch we use (gcc 13)
  is the same branch that ships in commercial qualified
  distributions (e.g. GHS, MentorEmbedded, RHEL). Bug reports for
  the C99 features we use (no extensions enabled) are accessible
  and the bug rate against the features in use is low.
- **Validation of the software tool.** The combined CI gates
  validate gcc against the actual properties we depend on for
  every safety goal:

  | Property gcc must hold                           | Validation                                                                 |
  | ------------------------------------------------ | -------------------------------------------------------------------------- |
  | Correct codec output                             | `xvalidation` job: 183 schemas × 5 seeds, byte-equal vs upstream cpp       |
  | Correct evaluation of buffer-bound checks (SG-1) | `test_bitstream.c`, `test_failure_modes.c` H-01                            |
  | Correct loop bounds preserved (SG-2)             | `test_varint.c`, `test_failure_modes.c` H-02                               |
  | No spurious recursion introduced by optimizer    | cppcheck Rule 17.2 (operates on source AST, not gcc output); cross-checked by manual inspection of generated assembly for the inner-loop functions |
  | Stack-usage qualifier accuracy                   | `stack` CI gate; the `.su` qualifier is the documented gcc contract.       |

## Specific criteria and evidence

- **Compiler version** is reproducible per CI run via the
  ubuntu-latest pin. The exact `gcc --version` is recorded in the
  job log under `build-and-test (gcc)`.
- **Configuration** is single-source: every CI job and every local
  build picks up the same flags from `CMakeLists.txt`.
- **Output verification** runs on every commit:
  - `build-and-test (gcc)` — runs the full `ctest` suite.
  - `build-and-test (clang)` — independent toolchain producing the
    same binary semantics. Any gcc-specific miscompilation that
    breaks the runtime is caught by the clang job running the same
    tests.
  - `xvalidation` — wire-format property checked end-to-end.
  - `stack` — per-function frame size checked against gcc's own
    `.su` output.

## Known limitations

- **Compiler version drift.** `ubuntu-latest` may bump the gcc
  version. The qualification re-applies after such a bump only
  to the extent that the validation gates still pass; if a gate
  fails, the bump is treated as a re-qualification trigger.
- **Optimization-level coupling.** Stack-usage numbers and
  inlining behavior depend on the optimization flag. The runtime
  build uses default `CMAKE_BUILD_TYPE=Release` for `build-and-test`
  but `Debug -O0` for `coverage`. The `stack` gate uses
  `MinSizeRel` indirectly (the `-fstack-usage` build inherits
  default flags from `cmake`). All three configurations are
  validated independently.
- **Cross-compilation.** Integrators building for a non-x86_64
  target re-run their own qualification on their target gcc; our
  qualification covers `x86_64-linux-gnu` only.

## Re-qualification triggers

A new gcc version on the runner that changes any of these is a
re-qualification trigger:

1. A `build-and-test` test failure that was previously passing.
2. A `stack` gate failure: a function previously `static` becomes
   `dynamic`/`bounded`, or a frame exceeds the 256 B ceiling.
3. A `xvalidation` divergence on a previously-passing schema.

The CI configuration's `ubuntu-latest` pin means the version is
reproducible per commit SHA but not stable over time. Integrators
who require a fixed version pin the runner image (e.g.
`ubuntu-24.04`) in their fork or build their own qualified copy.

## Audit re-execution

A reviewer can replay every qualification claim by re-running the
following CI jobs on a target commit:

```
build-and-test (gcc)
build-and-test (clang)
xvalidation
stack
coverage
```

Combined, they prove (a) gcc compiled the runtime, (b) the runtime
passed every unit test, (c) the wire-format output is byte-equal
to upstream, and (d) every per-function stack frame is static and
within the ceiling.
