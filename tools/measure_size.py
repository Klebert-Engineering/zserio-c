#!/usr/bin/env python3
"""
Measure section sizes of the compiled zserio-c runtime library.

Configures and builds the runtime in a dedicated `build-size/` directory
with `-Os -ffunction-sections -fdata-sections`, runs `size --format=sysv`
on the resulting static archive, and emits a JSON document to stdout
with per-section totals plus a per-object-file breakdown.

The output structure is:

    {
        "totals": {".text": <bytes>, ".data": <bytes>, ".bss": <bytes>, ...},
        "per_file": {
            "bitstream.c.o": {".text": <bytes>, ...},
            ...
        }
    }

Used by `tools/check_size_regression.py` and the CI `size` job to gate
REQ-PF-002 (no >=5% growth between consecutive commits to main on any
tracked section).

Verifies: REQ-PF-002
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO / "build-size"
LIB_REL = Path("runtime") / "libzserio_c_runtime.a"

# Sections we report. Anything outside this list is summed under
# "other" so the regression gate can never miss a category.
TRACKED_SECTIONS = (".text", ".data", ".rodata", ".bss")


def _run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, **kwargs)


def configure_and_build() -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    BUILD_DIR.mkdir()
    _run([
        "cmake",
        "-S", str(REPO),
        "-B", str(BUILD_DIR),
        "-DCMAKE_BUILD_TYPE=MinSizeRel",
        "-DZSERIO_C_BUILD_TESTS=OFF",
        "-DZSERIO_C_WERROR=OFF",
        # `-fno-asynchronous-unwind-tables -fno-unwind-tables` drop the
        # .eh_frame section. The runtime never throws (REQ-FS-002), so
        # the unwind tables are pure overhead — and worse, their size is
        # sensitive to gcc version, which makes the regression gate
        # noisy across toolchain bumps.
        "-DCMAKE_C_FLAGS=-Os -ffunction-sections -fdata-sections "
            "-fno-asynchronous-unwind-tables -fno-unwind-tables",
    ], stdout=subprocess.DEVNULL)
    _run([
        "cmake", "--build", str(BUILD_DIR), "--target", "zserio_c_runtime",
    ], stdout=subprocess.DEVNULL)


def measure(lib_path: Path) -> dict:
    """Parse `size --format=sysv <lib>` output and aggregate."""
    out = _run(
        ["size", "--format=sysv", str(lib_path)],
        capture_output=True, text=True,
    ).stdout

    files: dict[str, dict[str, int]] = {}
    current_file: str | None = None

    for raw in out.splitlines():
        line = raw.rstrip()
        if not line:
            continue
        if line.endswith(":"):
            # New object file. GNU `size --format=sysv` prints lines like
            #   "allocator.c.o   (ex /path/lib.a):"
            # We want the bare object name, before the parenthesis.
            head = line[:-1].rstrip()
            if "(" in head:
                head = head.split("(", 1)[0].rstrip()
            current_file = os.path.basename(head)
            files[current_file] = {}
            continue
        if current_file is None or not line.lstrip().startswith("."):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        section = parts[0]
        try:
            size = int(parts[1])
        except ValueError:
            continue
        files[current_file][section] = files[current_file].get(section, 0) + size

    return files


def _section_bucket(section: str) -> str:
    """Group `-ffunction-sections` / `-fdata-sections` sections into the
    coarse buckets we track. e.g. `.text.foo` and `.text.bar` both go
    under `.text`. `.data.rel.ro*` is read-only-after-relocation and is
    counted under `.rodata` rather than `.data`. Anything outside the
    tracked set goes to `other`."""
    if section.startswith(".data.rel.ro"):
        return ".rodata"
    for tracked in TRACKED_SECTIONS:
        if section == tracked or section.startswith(tracked + "."):
            return tracked
    return "other"


def aggregate(per_file: dict[str, dict[str, int]]) -> dict[str, int]:
    totals: dict[str, int] = {}
    for sections in per_file.values():
        for section, size in sections.items():
            key = _section_bucket(section)
            totals[key] = totals.get(key, 0) + size
    # Ensure every tracked section appears, even if zero.
    for section in TRACKED_SECTIONS:
        totals.setdefault(section, 0)
    return totals


def main() -> int:
    configure_and_build()
    lib = BUILD_DIR / LIB_REL
    if not lib.is_file():
        print(f"library not found at {lib}", file=sys.stderr)
        return 2
    per_file = measure(lib)
    totals = aggregate(per_file)
    json.dump(
        {"totals": totals, "per_file": per_file},
        sys.stdout, indent=2, sort_keys=True,
    )
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
