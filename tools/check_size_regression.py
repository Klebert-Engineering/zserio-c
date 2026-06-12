#!/usr/bin/env python3
"""
Compare current size measurement against the committed baseline.

Reads `docs/benchmarks/sizes.baseline.json` (last accepted size) and
the current measurement (passed as a CLI argument), prints a per-
section diff, and fails if any tracked section in `totals` has grown
by more than `THRESHOLD_PCT`. Sections that shrink or stay the same
are reported but never fail the gate.

When the current measurement file equals the baseline, this is a
no-op pass (zero deltas).

When the baseline does not yet exist, the script writes the current
measurement to the baseline path and exits successfully (initial-
seeding behavior).

Verifies: REQ-PF-002
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "docs" / "benchmarks" / "sizes.baseline.json"
THRESHOLD_PCT = 5.0


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: check_size_regression.py <current.json>", file=sys.stderr)
        return 2

    current = json.loads(Path(argv[1]).read_text())

    if not BASELINE.exists():
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        BASELINE.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n")
        print(f"Initial baseline seeded at {BASELINE.relative_to(REPO)}.")
        return 0

    baseline = json.loads(BASELINE.read_text())

    # Sections that are *gated* by the regression check. `other` (ELF
    # metadata: .comment, .note.GNU-stack, debug-info residue) is
    # reported for visibility but not gated — it varies across
    # toolchain versions and isn't safety-relevant. .text, .rodata,
    # .data, .bss are the binary-size dimensions we actually care about.
    GATED_SECTIONS = {".text", ".rodata", ".data", ".bss"}

    print(f"{'section':16s}  {'baseline':>10s}  {'current':>10s}  {'delta %':>10s}  gated")
    print("-" * 60)
    failed = False
    for section, current_size in sorted(current["totals"].items()):
        baseline_size = baseline["totals"].get(section, 0)
        if baseline_size == 0:
            change_pct = float("inf") if current_size > 0 else 0.0
            change_str = "+inf" if current_size > 0 else "+0.0"
        else:
            change_pct = (current_size - baseline_size) / baseline_size * 100.0
            change_str = f"{change_pct:+.2f}"
        gated = section in GATED_SECTIONS
        marker = ""
        if gated and change_pct > THRESHOLD_PCT:
            marker = "  REGRESSION"
            failed = True
        gated_str = "yes" if gated else "no"
        print(f"{section:16s}  {baseline_size:10d}  {current_size:10d}  {change_str:>10s}  {gated_str}{marker}")

    if failed:
        print(f"\nFAIL: at least one tracked section grew by more than {THRESHOLD_PCT}%.")
        print("To accept the change, run `tools/measure_size.py > docs/benchmarks/sizes.baseline.json`")
        print("in the same commit (with a justification in the commit message).")
        return 1
    print(f"\nOK: no tracked section grew by more than {THRESHOLD_PCT}%.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
