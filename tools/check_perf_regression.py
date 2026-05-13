#!/usr/bin/env python3
"""
Compare a benchmark JSON against the committed baseline.

Reads `docs/benchmarks/perf.baseline.json` (last accepted numbers)
and a current measurement file (positional argument), prints a per-
workload diff, and fails when any zserio-c metric has regressed by
more than `REGRESSION_PCT_FAIL` percent or has improved/regressed
by more than `REGRESSION_PCT_REBASELINE` (the larger threshold is
informational — the gate only fails on regressions).

The regression check is **us vs. our own baseline**, not us vs.
upstream cpp. The cpp ratio is reported for visibility but is not
itself a gate; bringing the ratio above 1.0 is the Phase 6
optimisation work that this gate measures the progress of.

Public CI runner noise on the ubuntu-latest image is significant.
The default threshold is intentionally loose (15%); a stricter
gate is a Phase 6 follow-up once we have noise data from real
runs.

Verifies: REQ-PF-001
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "docs" / "benchmarks" / "perf.baseline.json"

REGRESSION_PCT_FAIL = 15.0      # fail CI on a slowdown > this
REGRESSION_PCT_REBASELINE = 30.0  # informational warning


def fmt_pct(pct: float) -> str:
    sign = "+" if pct >= 0 else ""
    return f"{sign}{pct:.1f}%"


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: check_perf_regression.py <current.json>", file=sys.stderr)
        return 2

    current = json.loads(Path(argv[1]).read_text())

    if not BASELINE.exists():
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        BASELINE.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n")
        print(f"Initial baseline seeded at {BASELINE.relative_to(REPO)}.")
        return 0

    baseline = json.loads(BASELINE.read_text())

    print(f"{'workload':18s}  {'metric':18s}  {'baseline ns':>12s}  {'current ns':>12s}  "
          f"{'Δ %':>8s}  {'cpp ratio':>10s}")
    print("-" * 92)

    failed = False
    for name, current_wl in sorted(current.get("workloads", {}).items()):
        baseline_wl = baseline.get("workloads", {}).get(name)
        if baseline_wl is None:
            print(f"{name:18s}  (new workload — initial entry, no comparison)")
            continue
        for metric in ("encode_ns_per_op", "decode_ns_per_op"):
            cur = current_wl["zserio_c"][metric]
            base = baseline_wl["zserio_c"][metric]
            if base <= 0:
                pct = 0.0
            else:
                pct = (cur - base) / base * 100.0
            kind = "encode" if metric.startswith("encode") else "decode"
            cpp_ratio = current_wl["ratio_cpp_to_c"][kind]
            mark = ""
            if pct > REGRESSION_PCT_FAIL:
                mark = "  REGRESSION"
                failed = True
            elif pct > REGRESSION_PCT_REBASELINE:
                # In practice REGRESSION_PCT_REBASELINE > REGRESSION_PCT_FAIL,
                # so this branch is unreachable here; kept for clarity if the
                # thresholds change.
                mark = "  REVIEW"
            elif pct < -REGRESSION_PCT_REBASELINE:
                mark = "  ↓ rebaseline?"
            print(f"{name:18s}  {metric:18s}  {base:>12.2f}  {cur:>12.2f}  "
                  f"{fmt_pct(pct):>8s}  {cpp_ratio:>10.3f}{mark}")

    if failed:
        print(f"\nFAIL: at least one zserio_c metric regressed by more than "
              f"{REGRESSION_PCT_FAIL:.0f}%.")
        print("To accept the change, run the benchmark and replace the baseline:")
        print("  bash tests/benchmarks/run_benchmark.sh > docs/benchmarks/perf.baseline.json")
        return 1
    print(f"\nOK: no zserio_c metric regressed by more than {REGRESSION_PCT_FAIL:.0f}%.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
