#!/usr/bin/env python3
"""
Measure stack usage of the zserio-c runtime library.

Configures and builds the runtime in a dedicated `build-stack/`
directory with ``-fstack-usage`` AND ``-fcallgraph-info=su,da``, then
runs two analyzes against the resulting ``.su`` (per-function frame
sizes) and ``.ci`` (per-TU call graphs, VCG format) files:

  1. **Per-function gate.** Fails when any function's frame exceeds
     ``MAX_FRAME_BYTES`` or is marked ``dynamic`` / ``bounded`` (the
     compiler could not pin a constant frame size — alloca / VLA).
  2. **Transitive worst-case gate.** Unions the per-TU call graphs,
     DFS-checks for cycles (REQ-FS-003 forbids recursion), and runs
     a longest-weighted-path DP over the DAG (weights = frame size).
     Fails when the worst total along any call chain exceeds
     ``MAX_PATH_BYTES``.

This is the runtime side of the stack-budget analysis for hazard
H-09 (see ``docs/safety-case/hazard-analysis.md``). Together the
two gates replace the doc's earlier conservative
``max_frame × depth`` ceiling with an exact, mechanically-checked
upper bound on the runtime's stack usage.

Verifies: REQ-FS-003
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO / "build-stack"

# Per-function ceiling. Current observed max in the runtime is
# 160 bytes (zserio_read_extern). 256 bytes leaves comfortable
# headroom while keeping new code from silently growing frames.
MAX_FRAME_BYTES = 256

# Transitive worst-case ceiling. Current observed max along any
# call chain rooted at a defined runtime function is 512 bytes
# (see `docs/safety-case/stack-budget.md` for the analysis). 1024
# leaves ~2× headroom — the same headroom philosophy as
# MAX_FRAME_BYTES. The bound is built without LTO, so it is a
# pessimistic upper bound: real production builds that enable
# `-flto` collapse the static-inline impls into their callers and
# the worst-case path is smaller.
MAX_PATH_BYTES = 1024

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
            # `-fstack-usage`        — per-function `.su` files (frames)
            # `-fcallgraph-info=su,da` — per-TU `.ci` files (call graph
            #   in VCG format, with frame sizes + dynamic-object counts
            #   embedded in each node label). Both files are produced
            #   in one build pass.
            "-DCMAKE_C_FLAGS=-fstack-usage -fcallgraph-info=su,da",
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


# --- Transitive call-graph analysis (parse .ci, longest path) -------------
#
# Each .ci file is one TU's call graph in VCG (Visualization of Compiler
# Graphs) format. Lines we care about:
#
#   node: { title: "<name>"  label: "<name>\n<loc>\n<N> bytes (<qual>)\n..." }
#   edge: { sourcename: "<caller>"  targetname: "<callee>"  label: "..." }
#
# Static functions get prefixed by their source path in the title
# (e.g. ".../varint.c:read_varuint_impl") so cppcheck-graph doesn't
# collide them across TUs. Globals use the bare symbol name. Edges
# reference whichever form the callee was declared with.
#
# Calls to symbols not defined in any of our TUs (libc, gcc builtins)
# appear as nodes whose label has no "N bytes (qual)" — we treat them
# as 0-frame externals. Their actual stack cost is the integrator's
# libc to size; documented in stack-budget.md.

NODE_RE = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"\s*label:\s*"([^"]+)"')
EDGE_RE = re.compile(r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s*targetname:\s*"([^"]+)"')
FRAME_RE = re.compile(r'(\d+)\s+bytes\s+\((\w+)\)')


def parse_ci_files(root: Path) -> tuple[dict, dict]:
    """Union the call graph across every TU's `.ci` under `root`.

    Returns ``(nodes, edges)`` where:
      * ``nodes[name] = {"frame": int, "qualifier": str, "defined": bool}``
      * ``edges[caller] = set(callees)``
    """
    nodes: dict[str, dict] = {}
    edges: dict[str, set[str]] = defaultdict(set)

    # Restrict to the runtime target's .ci files. CMake's CompilerId
    # probes also drop .ci alongside the real ones — those nodes
    # (`main` from a Hello-World probe) would pollute the analysis.
    ci_files = [
        p for p in root.rglob('*.ci')
        if 'CMakeFiles/zserio_c_runtime' in str(p)
    ]
    for ci in ci_files:
        for line in ci.read_text().splitlines():
            m = NODE_RE.search(line)
            if m:
                name, label = m.group(1), m.group(2)
                fm = FRAME_RE.search(label)
                if fm:
                    frame, qual = int(fm.group(1)), fm.group(2)
                    # If we see the same node from another TU, prefer
                    # the defined record (a global may also appear as
                    # an external in TUs that only call it).
                    if name not in nodes or not nodes[name]['defined']:
                        nodes[name] = {'frame': frame, 'qualifier': qual, 'defined': True}
                else:
                    nodes.setdefault(name, {'frame': 0, 'qualifier': 'external', 'defined': False})
                continue
            m = EDGE_RE.search(line)
            if m:
                edges[m.group(1)].add(m.group(2))

    # Any callee that doesn't have its own node entry gets a 0-frame
    # external placeholder so the traversal can resolve it.
    for callees in list(edges.values()):
        for c in callees:
            if c not in nodes:
                nodes[c] = {'frame': 0, 'qualifier': 'external', 'defined': False}

    return nodes, dict(edges)


def find_cycle(nodes: dict, edges: dict) -> list[str] | None:
    """Return one cycle as a list of node names, or None if the graph is a DAG."""
    WHITE, GRAY, BLACK = 0, 1, 2
    color = {n: WHITE for n in nodes}

    sys.setrecursionlimit(max(sys.getrecursionlimit(), len(nodes) * 4))

    def dfs(u: str, path: list[str]) -> list[str] | None:
        color[u] = GRAY
        path.append(u)
        for v in edges.get(u, ()):
            if v not in color:
                continue
            if color[v] == GRAY:
                idx = path.index(v)
                return path[idx:] + [v]
            if color[v] == WHITE:
                c = dfs(v, path)
                if c is not None:
                    return c
        color[u] = BLACK
        path.pop()
        return None

    for n in nodes:
        if color[n] == WHITE:
            c = dfs(n, [])
            if c is not None:
                return c
    return None


def compute_transitive(nodes: dict, edges: dict) -> list[dict]:
    """Longest-weighted-path DP from each defined node.

    Returns a list of dicts (one per defined node) with keys
    ``function``, ``total``, ``path`` (list of node names from the
    entry to the leaf), sorted by ``total`` descending. Precondition:
    no cycle (caller checks).
    """
    memo: dict[str, tuple[int, list[str]]] = {}
    sys.setrecursionlimit(max(sys.getrecursionlimit(), len(nodes) * 4))

    def longest(u: str) -> tuple[int, list[str]]:
        if u in memo:
            return memo[u]
        best_extra = 0
        best_callee_path: list[str] = []
        for v in edges.get(u, ()):
            ext, p = longest(v)
            if ext > best_extra:
                best_extra = ext
                best_callee_path = p
        total = nodes[u]['frame'] + best_extra
        memo[u] = (total, [u] + best_callee_path)
        return memo[u]

    out: list[dict] = []
    for n, meta in nodes.items():
        if not meta['defined']:
            continue
        total, path = longest(n)
        out.append({'function': n, 'total': total, 'path': path})
    out.sort(key=lambda r: -r['total'])
    return out


def shortname(name: str) -> str:
    """Strip the gcc-emitted file-path prefix from a static-function key."""
    return name.rsplit('/', 1)[-1] if '/' in name else name


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
    parser.add_argument("--top-paths", type=int, default=10,
                        help="how many worst-case paths to print (default 10)")
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

    # --- Transitive worst-case via .ci call graph -----------------------
    nodes, edges = parse_ci_files(runtime_dir)
    cycle = find_cycle(nodes, edges)
    transitive: list[dict] = []
    max_path = 0
    over_path: list[dict] = []
    if cycle is None and nodes:
        transitive = compute_transitive(nodes, edges)
        if transitive:
            max_path = transitive[0]["total"]
            over_path = [t for t in transitive if t["total"] > MAX_PATH_BYTES]

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
        "max_path_bytes": max_path,
        "path_threshold_bytes": MAX_PATH_BYTES,
        "cycle": cycle,
        "over_path_threshold": [
            {"entry": t["function"], "total": t["total"],
             "path": [shortname(p) for p in t["path"]]}
            for t in over_path
        ],
    }

    if args.json:
        json.dump({"summary": summary, "top": rows[: args.top],
                   "top_paths": transitive[: args.top_paths]},
                  sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        print(f"max frame: {max_frame} bytes (threshold: {MAX_FRAME_BYTES})")
        print(f"functions analyzed: {len(rows)}")
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

        # --- transitive section -------------------------------------------
        print()
        print(f"max call-chain stack: {max_path} bytes "
              f"(threshold: {MAX_PATH_BYTES})")
        if cycle is not None:
            print()
            print("FAIL: cycle in the call graph (REQ-FS-003 forbids recursion):")
            print("  " + " -> ".join(shortname(n) for n in cycle))
        elif transitive:
            print()
            print(f"top {min(args.top_paths, len(transitive))} worst-case paths:")
            for t in transitive[: args.top_paths]:
                framed = " -> ".join(
                    f"{shortname(p)}({nodes[p]['frame']})" for p in t["path"]
                )
                mark = " " if t["total"] <= MAX_PATH_BYTES else "*"
                print(f"{t['total']:>5} B{mark} {framed}")
            if over_path:
                print()
                print(f"FAIL: {len(over_path)} call chain(s) exceed "
                      f"{MAX_PATH_BYTES} bytes:")
                for t in over_path:
                    print(f"  {shortname(t['function'])}: {t['total']} bytes")
        else:
            print("(no .ci data — transitive check skipped)")

        if (not bad_qualifier and not over_threshold
                and cycle is None and not over_path):
            print()
            print("OK: every function has a static frame within the threshold,")
            print("    and every call chain stays within the transitive ceiling.")

    if args.strict and (bad_qualifier or over_threshold
                        or cycle is not None or over_path):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
