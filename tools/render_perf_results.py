#!/usr/bin/env python3
"""Render `docs/benchmarks/RESULTS.md` from the per-host baseline JSONs.

Reads every `docs/benchmarks/perf.baseline.<host>.json` and emits a
Markdown table per host plus a combined summary. The output is
deterministic for a given baseline set, so the file can be checked in
and the script re-run after each baseline refresh to keep the
narrative tables (in README / CHANGELOG / safety-case) and the
gate-evidence file in sync.

Run from the repository root:

    python3 tools/render_perf_results.py

The script writes to `docs/benchmarks/RESULTS.md` and prints a one-line
summary to stdout. It is intentionally output-only — never reads any
state outside the baseline JSONs — so it is safe to re-run from CI
or a pre-commit hook.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Iterable


REPO_ROOT = Path(__file__).resolve().parent.parent
BASELINE_DIR = REPO_ROOT / "docs" / "benchmarks"
OUTPUT_PATH = BASELINE_DIR / "RESULTS.md"

# Hosts the perf CI matrix actually runs. Historical baselines
# (e.g. `perf.baseline.synology-d1508.json`) stay in-tree for
# traceability per `docs/benchmarks/methodology.md` but don't appear
# in the rendered results table; they're explicitly retired hardware.
ACTIVE_HOSTS = ("primary", "pi4")


def discover_hosts() -> list[str]:
    pat = "perf.baseline.*.json"
    available = {p.stem.split(".", maxsplit=2)[-1] for p in BASELINE_DIR.glob(pat)}
    return [h for h in ACTIVE_HOSTS if h in available]


def load_baseline(host: str) -> dict[str, Any]:
    path = BASELINE_DIR / f"perf.baseline.{host}.json"
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)


def verdict(ratio_decode: float) -> str:
    if ratio_decode >= 1.5:
        return "clear win"
    if ratio_decode >= 1.05:
        return "win"
    if ratio_decode >= 0.95:
        return "parity"
    if ratio_decode >= 0.80:
        return "narrow loss"
    return "loss"


def format_host_table(host: str, data: dict[str, Any]) -> str:
    workloads = data["workloads"]
    rows = []
    for name in sorted(workloads):
        w = workloads[name]
        c = w["zserio_c"]["decode_ns_per_op"]
        cpp = w["zserio_cpp"]["decode_ns_per_op"]
        r = w["ratio_cpp_to_c"]["decode"]
        rows.append((name, c, cpp, r, verdict(r)))
    lines = [
        f"### Host `{host}`",
        "",
        "Decode `ns/op` per workload (median of 5 runs, sorted by ratio).",
        "",
        "| Workload | zserio_c ns | zserio_cpp ns | cpp/c | Verdict |",
        "| -------- | ----------: | ------------: | ----: | ------- |",
    ]
    for name, c, cpp, r, v in sorted(rows, key=lambda x: -x[3]):
        lines.append(
            f"| `{name}` | {c:,.0f} | {cpp:,.0f} | {r:.3f} | {v} |"
        )
    lines.append("")
    return "\n".join(lines)


def format_summary(hosts: Iterable[str]) -> str:
    rows: dict[str, dict[str, float]] = {}
    for host in hosts:
        for name, w in load_baseline(host)["workloads"].items():
            rows.setdefault(name, {})[host] = w["ratio_cpp_to_c"]["decode"]
    host_list = sorted(rows[next(iter(rows))].keys())

    lines = [
        "## Summary — cpp/c decode ratio per host",
        "",
        "Values ≥ 1.0 mean zserio-c is faster than upstream cpp at the",
        "host's defined allocator strategy (libc-vs-libc by default;",
        "the `_arena` workloads use a bump arena vs cpp's `std::pmr`).",
        "",
        "| Workload | " + " | ".join(f"`{h}`" for h in host_list) + " |",
        "| -------- | " + " | ".join("---:" for _ in host_list) + " |",
    ]
    for name in sorted(rows):
        cells = " | ".join(
            f"{rows[name][h]:.3f}" if h in rows[name] else "—"
            for h in host_list
        )
        lines.append(f"| `{name}` | {cells} |")
    lines.append("")

    win_counts = {h: 0 for h in host_list}
    total = 0
    for name, by_host in rows.items():
        total += 1
        for h, ratio in by_host.items():
            if ratio >= 1.0:
                win_counts[h] += 1
    lines.append(
        "**Headline:** "
        + "; ".join(
            f"`{h}` beats or ties cpp on {win_counts[h]} / {total} workloads"
            for h in host_list
        )
        + "."
    )
    lines.append("")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    hosts = discover_hosts()
    if not hosts:
        print(f"no baselines found under {BASELINE_DIR}", file=sys.stderr)
        return 1

    header = [
        "# Performance results",
        "",
        "Auto-generated from `docs/benchmarks/perf.baseline.<host>.json`",
        "by `tools/render_perf_results.py`. Do not edit by hand — re-run",
        "the script after a baseline refresh.",
        "",
    ]

    out_parts = ["\n".join(header), format_summary(hosts)]
    for host in hosts:
        out_parts.append(format_host_table(host, load_baseline(host)))

    OUTPUT_PATH.write_text("\n".join(out_parts), encoding="utf-8")
    print(f"wrote {OUTPUT_PATH.relative_to(REPO_ROOT)} ({len(hosts)} host(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
