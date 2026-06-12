#!/usr/bin/env python3
"""
Compare a benchmark JSON against the per-host baseline.

Reads `docs/benchmarks/perf.baseline.<host>.json` (last accepted
numbers for the host the current run measured on) and a current
measurement file (positional argument), prints a per-workload diff,
and fails when any zserio-c metric has regressed by more than
`REGRESSION_PCT_FAIL` percent.

The host identifier comes from the `PERF_HOST` env var, set by the
CI step from the runner's host label. Cross-host comparison is
nonsense (different ISA / microarch / noise floor); each host
compares only against its own history. The set of known baselines
under `docs/benchmarks/perf.baseline.*.json` is the multi-host
record. See `docs/benchmarks/methodology.md` for the full policy.

If `docs/benchmarks/perf.baseline.<host>.json` doesn't exist (new
host, or first run after a workload-set change), the script seeds
it from the current run's JSON and exits OK — same seed-on-miss
as the initial baseline behavior.

The regression check is **us vs. our own per-host baseline**, not
us vs. upstream cpp. The cpp ratio is reported for visibility but
is not itself a gate; bringing the ratio above 1.0 is the
optimization work that this gate measures the progress of.

Verifies: REQ-PF-001
"""

from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE_DIR = REPO / "docs" / "benchmarks"

# A clear, slightly loose threshold. Each host's noise floor is
# documented in methodology.md; if a future host runs noisier than
# this, the gate should be widened with a justification rather than
# silently rebaselined.
REGRESSION_PCT_FAIL = 15.0      # fail CI on a slowdown > this
REGRESSION_PCT_REBASELINE = 30.0  # informational rebaseline hint


# Host identifiers must be filename-safe and stable.
_HOST_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")


def baseline_path_for(host: str) -> Path:
    return BASELINE_DIR / f"perf.baseline.{host}.json"


def fmt_pct(pct: float) -> str:
    sign = "+" if pct >= 0 else ""
    return f"{sign}{pct:.1f}%"


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: PERF_HOST=<host> check_perf_regression.py <current.json>",
              file=sys.stderr)
        return 2

    host = os.environ.get("PERF_HOST", "").strip()
    if not host:
        print("PERF_HOST env var is required (e.g., 'primary' / 'pi5' / 'jetson').",
              file=sys.stderr)
        print("It identifies which perf.baseline.<host>.json the current run is "
              "compared against. See docs/benchmarks/methodology.md.",
              file=sys.stderr)
        return 2
    if not _HOST_PATTERN.match(host):
        print(f"PERF_HOST='{host}' is not a valid host identifier "
              f"(must match {_HOST_PATTERN.pattern}).", file=sys.stderr)
        return 2

    current = json.loads(Path(argv[1]).read_text())
    baseline_file = baseline_path_for(host)

    if not baseline_file.exists():
        baseline_file.parent.mkdir(parents=True, exist_ok=True)
        baseline_file.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n")
        print(f"Initial baseline for host '{host}' seeded at "
              f"{baseline_file.relative_to(REPO)}.")
        return 0

    baseline = json.loads(baseline_file.read_text())

    print(f"host: {host}   (baseline: {baseline_file.relative_to(REPO)})")
    print()
    print(f"{'workload':18s}  {'metric':18s}  {'baseline ns':>12s}  "
          f"{'current ns':>12s}  {'Δ %':>8s}  {'cpp ratio':>10s}")
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
              f"{REGRESSION_PCT_FAIL:.0f}% on host '{host}'.")
        print("To accept the change, run the benchmark on this host and replace "
              "its baseline:")
        print(f"  PERF_HOST={host} tests/benchmarks/run_benchmark.sh "
              f"> {baseline_file.relative_to(REPO)}")
        return 1
    print(f"\nOK: no zserio_c metric regressed by more than "
          f"{REGRESSION_PCT_FAIL:.0f}% on host '{host}'.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
