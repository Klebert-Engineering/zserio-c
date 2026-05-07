---
id: REQ-PF-002
title: Tracked code size, regression-gated
type: performance
status: accepted
phase_target: 1
verification: test
---

## Statement

The compiled size of the zserio-c runtime library and of generated code
for the benchmark schemas (per REQ-PF-001) SHALL be measured on every
commit and recorded. A size regression of 5% or more on any tracked
artifact, between two consecutive commits to `main`, SHALL fail CI
unless accompanied by a justification recorded in the PR description.

## Rationale

zserio-c is meant to run in resource-constrained automotive ECUs, where
flash and RAM budgets are real and small. Code-size regressions tend to
sneak in invisibly across many small changes and are far easier to
prevent than to remove. Gating them in CI forces the conversation to
happen at the time of the change rather than during a panic before
release.

## Verification

- A CI step builds the runtime and the benchmark schemas at `-Os` for a
  representative cross-target (initially `arm-none-eabi-gcc` with
  `-mcpu=cortex-m4`) and writes section sizes to a JSON artifact.
- A comparison step diffs against the artifact on the merge base and
  fails if any section grows by ≥5%.

## Notes

- The 5% threshold is a per-section threshold, not aggregate. A small
  function growing by 5% against a small base is still flagged; the
  PR can justify it explicitly if accepted.
- Cross-target choice may evolve as we learn which targets matter to
  consumers; the requirement is the gate, not the specific target.
