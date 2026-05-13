#!/usr/bin/env python3
"""
Measure per-function stack usage of the zserio-c runtime library.

Configures and builds the runtime in a dedicated `build-stack/`
directory with ``-fstack-usage``, parses the resulting ``.su`` files,
and either prints a per-function report (default) or fails the gate
when any of the following is true (``--strict``):

  * a function's stack frame exceeds the per-function threshold
    (``MAX_FRAME_BYTES``);
  * a function's frame is marked ``dynamic`` or ``bounded`` (i.e. the
    compiler could not determine a constant frame size — alloca,
    variable-length arrays, or similar).

This is the runtime side of the stack-budget analysis for hazard
H-09 (see ``docs/safety-case/hazard-analysis.md``). Combined with
REQ-FS-003 (no recursion), a static per-function cap implies a
finite worst-case total — the longest call chain times the largest
frame is a safe upper bound, and the absence of dynamic frames
means the compiler verified each frame at build time.

Verifies: REQ-FS-003
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO / "build-stack"

# Per-function ceiling. Current observed max in the runtime is
# 144 bytes (zserio_read_extern). 256 bytes leaves comfortable
# headroom while keeping new code from silently growing frames.
MAX_FRAME_BYTES = 256

# Qualifiers gcc may attach to a `.su` line. `static` is the only
# safety-acceptable value: a compile-time constant. `dynamic` /
# `bounded` mean the compiler could not pin the size at build time
# (alloca, VLAs, ...). REQ-FS-005 + REQ-FS-003 together imply we
# never produce these.
ACCEPTABLE_QUALIFIERS = {"static"}


def configure_and_build() -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    BUILD_DIR.mkdir()
    subprocess.run(
        [
            "cmake",
            "-S", str(REPO),
            "-B", str(BUILD_DIR),
            "-DZSERIO_C_BUILD_TESTS=OFF",
            "-DCMAKE_C_FLAGS=-fstack-usage",
        ],
        check=True, stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--target", "zserio_c_runtime"],
        check=True, stdout=subprocess.DEVNULL,
    )


def parse_su_files(root: Path) -> list[dict]:
    """Each .su line is `<file>:<lineno>:<col>:<func>\\t<bytes>\\t<qualifier>`.

    Returns a list of dicts sorted by descending frame size.
    """
    rows: list[dict] = []
    for su in root.rglob("*.su"):
        for raw in su.read_text().splitlines():
            line = raw.strip()
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            location, bytes_str, qualifier = parts[0], parts[1], parts[2]
            try:
                frame_bytes = int(bytes_str)
            except ValueError:
                continue
            # `location` is `<file>:<line>:<col>:<func>`. Pull the
            # function name off the right; everything else is path.
            loc_parts = location.rsplit(":", 3)
            if len(loc_parts) != 4:
                continue
            path, _line, _col, func = loc_parts
            rows.append({
                "function": func,
                "file": os.path.relpath(path, REPO) if path.startswith(str(REPO)) else path,
                "bytes": frame_bytes,
                "qualifier": qualifier,
            })
    rows.sort(key=lambda r: -r["bytes"])
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on any policy violation")
    parser.add_argument("--json", action="store_true",
                        help="emit machine-readable JSON to stdout")
    parser.add_argument("--no-build", action="store_true",
                        help="skip the cmake build (use existing .su files)")
    parser.add_argument("--top", type=int, default=20,
                        help="how many entries to print (default 20)")
    args = parser.parse_args()

    if not args.no_build:
        configure_and_build()

    runtime_dir = BUILD_DIR / "runtime"
    if not runtime_dir.is_dir():
        print(f"runtime build dir not found: {runtime_dir}", file=sys.stderr)
        return 2

    rows = parse_su_files(runtime_dir)
    if not rows:
        print("no .su files found — was -fstack-usage in effect?",
              file=sys.stderr)
        return 2

    max_frame = max(r["bytes"] for r in rows)
    bad_qualifier = [r for r in rows if r["qualifier"] not in ACCEPTABLE_QUALIFIERS]
    over_threshold = [r for r in rows if r["bytes"] > MAX_FRAME_BYTES]

    summary = {
        "max_frame_bytes": max_frame,
        "threshold_bytes": MAX_FRAME_BYTES,
        "total_functions": len(rows),
        "non_static_qualifier": [
            {"function": r["function"], "qualifier": r["qualifier"], "file": r["file"]}
            for r in bad_qualifier
        ],
        "over_threshold": [
            {"function": r["function"], "bytes": r["bytes"], "file": r["file"]}
            for r in over_threshold
        ],
    }

    if args.json:
        json.dump({"summary": summary, "top": rows[: args.top]},
                  sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        print(f"max frame: {max_frame} bytes (threshold: {MAX_FRAME_BYTES})")
        print(f"functions analysed: {len(rows)}")
        print()
        print(f"{'bytes':>6}  qualifier  function")
        print("-" * 70)
        for r in rows[: args.top]:
            mark = " " if (r["qualifier"] in ACCEPTABLE_QUALIFIERS
                           and r["bytes"] <= MAX_FRAME_BYTES) else "*"
            print(f"{r['bytes']:>6}  {r['qualifier']:9s}{mark} {r['function']}")
        if bad_qualifier:
            print()
            print(f"FAIL: {len(bad_qualifier)} function(s) have non-static frames:")
            for r in bad_qualifier:
                print(f"  {r['function']} [{r['qualifier']}] in {r['file']}")
        if over_threshold:
            print()
            print(f"FAIL: {len(over_threshold)} function(s) exceed "
                  f"{MAX_FRAME_BYTES} bytes:")
            for r in over_threshold:
                print(f"  {r['function']}: {r['bytes']} bytes  ({r['file']})")
        if not bad_qualifier and not over_threshold:
            print()
            print("OK: every function has a static frame within the threshold.")

    if args.strict and (bad_qualifier or over_threshold):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
