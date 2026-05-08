#!/usr/bin/env python3
"""
Verify line, branch, and function coverage thresholds from an lcov
`.info` tracefile.

The script parses the tracefile directly (not via `lcov --summary`) so
the thresholds and the line/branch/function aggregation are explicit
and deterministic. Counts are summed across SF records whose path
contains the `--path-filter` substring (default `runtime/`), so any
captures from the test sources or vendored Unity are excluded
regardless of the lcov capture flags used upstream.

Exits non-zero if any threshold is not met.

Verifies: REQ-FS-005
Verifies: REQ-FS-006
Verifies: REQ-FS-007
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Initial thresholds. These are conservative; tighten as confidence builds.
DEFAULT_LINE_PCT = 95.0
DEFAULT_BRANCH_PCT = 80.0
DEFAULT_FUNCTION_PCT = 100.0
DEFAULT_PATH_FILTER = "runtime/"


def parse_info(info_path: Path, path_filter: str) -> tuple[int, int, int, int, int, int, list[tuple[str, int, int, int, int]]]:
    """Sum LF, LH, BRF, BRH, FNF, FNH across SF records whose path
    contains `path_filter`. Also returns per-file (sf, LF, LH, BRF, BRH).
    """
    LF = LH = BRF = BRH = FNF = FNH = 0
    per_file: list[tuple[str, int, int, int, int]] = []
    sf: str | None = None
    keep = False
    f_LF = f_LH = f_BRF = f_BRH = 0

    for raw in info_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("SF:"):
            sf = line[3:]
            keep = path_filter in sf
            f_LF = f_LH = f_BRF = f_BRH = 0
            continue
        if line == "end_of_record":
            if keep and sf is not None:
                per_file.append((sf, f_LF, f_LH, f_BRF, f_BRH))
            sf = None
            keep = False
            continue
        if not keep:
            continue
        try:
            if line.startswith("LF:"):
                v = int(line[3:]); LF += v; f_LF = v
            elif line.startswith("LH:"):
                v = int(line[3:]); LH += v; f_LH = v
            elif line.startswith("BRF:"):
                v = int(line[4:]); BRF += v; f_BRF = v
            elif line.startswith("BRH:"):
                v = int(line[4:]); BRH += v; f_BRH = v
            elif line.startswith("FNF:"):
                FNF += int(line[4:])
            elif line.startswith("FNH:"):
                FNH += int(line[4:])
        except ValueError:
            continue

    return LF, LH, BRF, BRH, FNF, FNH, per_file


def pct(hit: int, total: int) -> float:
    return 100.0 * hit / total if total > 0 else 0.0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("info_file", type=Path)
    parser.add_argument("--path-filter", default=DEFAULT_PATH_FILTER)
    parser.add_argument("--line-threshold", type=float, default=DEFAULT_LINE_PCT)
    parser.add_argument("--branch-threshold", type=float, default=DEFAULT_BRANCH_PCT)
    parser.add_argument("--function-threshold", type=float, default=DEFAULT_FUNCTION_PCT)
    args = parser.parse_args(argv[1:])

    LF, LH, BRF, BRH, FNF, FNH, per_file = parse_info(args.info_file, args.path_filter)

    if LF == 0:
        print(f"FAIL: no source files matched '{args.path_filter}' in {args.info_file}",
              file=sys.stderr)
        return 1

    line_p = pct(LH, LF)
    br_p = pct(BRH, BRF)
    fn_p = pct(FNH, FNF)

    print(f"path filter: {args.path_filter!r}")
    print(f"{'file':45s}  {'lines':>16s}  {'branches':>16s}")
    print("-" * 80)
    for sf, fl, hl, fbr, hbr in sorted(per_file):
        short = sf
        if "/" in short:
            short = short.split("runtime/", 1)[-1] if "runtime/" in short else short
        line_s = f"{hl}/{fl} ({pct(hl, fl):.1f}%)"
        br_s = f"{hbr}/{fbr} ({pct(hbr, fbr):.1f}%)" if fbr > 0 else "-"
        print(f"{short[-45:]:45s}  {line_s:>16s}  {br_s:>16s}")
    print("-" * 80)
    print(f"{'TOTAL':45s}  {LH}/{LF} ({line_p:.2f}%)   {BRH}/{BRF} ({br_p:.2f}%)")
    print()
    print(f"functions: {FNH}/{FNF} ({fn_p:.2f}%)")
    print()
    print(f"thresholds: line >= {args.line_threshold:.1f}% / "
          f"branch >= {args.branch_threshold:.1f}% / "
          f"functions >= {args.function_threshold:.1f}%")
    print()

    failed = False
    if line_p < args.line_threshold:
        print(f"FAIL: line coverage {line_p:.2f}% < {args.line_threshold:.1f}%")
        failed = True
    if BRF > 0 and br_p < args.branch_threshold:
        print(f"FAIL: branch coverage {br_p:.2f}% < {args.branch_threshold:.1f}%")
        failed = True
    if FNF > 0 and fn_p < args.function_threshold:
        print(f"FAIL: function coverage {fn_p:.2f}% < {args.function_threshold:.1f}%")
        failed = True

    if failed:
        return 1
    print("OK: all thresholds met.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
