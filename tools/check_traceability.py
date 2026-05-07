#!/usr/bin/env python3
"""Requirements traceability gate.

Walks ``requirements/`` (one markdown file per requirement, with YAML-style
frontmatter) and ``tests/`` (C sources containing ``Verifies: REQ-...``
markers in comments). Every ``accepted`` requirement whose ``phase_target``
is less than or equal to the current project phase must be referenced by
at least one such marker.

The current phase is read from the ``.phase`` file at the repository root
(a single digit). A requirement that has not yet reached its phase target
is reported as informational and does not fail the gate.

Exit codes:
    0 — gate passes
    1 — gate fails (missing test references for in-phase requirements)
    2 — malformed inputs (frontmatter parse error, etc.)

The script uses the standard library only. Keep it that way.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REQUIREMENTS_DIR = REPO / "requirements"
TESTS_DIR = REPO / "tests"
PHASE_FILE = REPO / ".phase"

REQ_ID_PATTERN = re.compile(r"^REQ-[A-Z]{2}-\d{3}$")
VERIFIES_PATTERN = re.compile(r"Verifies\s*:\s*(REQ-[A-Z]{2}-\d{3})")
FRONTMATTER_PATTERN = re.compile(r"^---\s*\n(.*?)\n---\s*\n", re.DOTALL)


@dataclass
class Requirement:
    id: str
    status: str
    phase_target: int
    path: Path


def _parse_frontmatter(text: str, path: Path) -> dict[str, str]:
    match = FRONTMATTER_PATTERN.search(text)
    if not match:
        raise SystemExit(f"{path}: no YAML frontmatter")
    data: dict[str, str] = {}
    for raw in match.group(1).splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            raise SystemExit(f"{path}: malformed frontmatter line: {raw!r}")
        key, value = line.split(":", 1)
        data[key.strip()] = value.strip()
    return data


def load_requirements() -> list[Requirement]:
    out: list[Requirement] = []
    for md in sorted(REQUIREMENTS_DIR.rglob("REQ-*.md")):
        fm = _parse_frontmatter(md.read_text(encoding="utf-8"), md)
        rid = fm.get("id", "")
        if not REQ_ID_PATTERN.match(rid):
            raise SystemExit(f"{md}: invalid id {rid!r}")
        if rid != md.stem:
            raise SystemExit(f"{md}: id {rid!r} does not match filename")
        status = fm.get("status", "")
        if status not in {"proposed", "accepted", "withdrawn"}:
            raise SystemExit(f"{md}: invalid status {status!r}")
        try:
            phase_target = int(fm.get("phase_target", "-1"))
        except ValueError:
            raise SystemExit(f"{md}: phase_target must be an integer")
        out.append(Requirement(id=rid, status=status, phase_target=phase_target, path=md))
    return out


def load_test_references() -> dict[str, list[Path]]:
    refs: dict[str, list[Path]] = {}
    if not TESTS_DIR.exists():
        return refs
    for src in TESTS_DIR.rglob("*"):
        if src.suffix not in {".c", ".h", ".cpp", ".hpp", ".cc"}:
            continue
        try:
            text = src.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for match in VERIFIES_PATTERN.finditer(text):
            refs.setdefault(match.group(1), []).append(src)
    return refs


def current_phase() -> int:
    if not PHASE_FILE.exists():
        raise SystemExit(f"missing {PHASE_FILE.relative_to(REPO)}")
    text = PHASE_FILE.read_text(encoding="utf-8").strip()
    if not text.isdigit():
        raise SystemExit(f"{PHASE_FILE.relative_to(REPO)}: must contain a single integer")
    return int(text)


def main() -> int:
    phase = current_phase()
    reqs = load_requirements()
    refs = load_test_references()

    print(f"current phase: {phase}")
    print(f"requirements:  {len(reqs)}")
    print(f"test files referencing requirements: "
          f"{len({p for paths in refs.values() for p in paths})}")
    print()

    enforced_missing: list[Requirement] = []
    informational: list[Requirement] = []

    for req in reqs:
        if req.status != "accepted":
            continue
        if req.id in refs:
            continue
        if req.phase_target <= phase:
            enforced_missing.append(req)
        else:
            informational.append(req)

    if informational:
        print("Pending (phase not yet reached):")
        for req in informational:
            print(f"  {req.id}  [phase {req.phase_target}]  {req.path.relative_to(REPO)}")
        print()

    if enforced_missing:
        print("FAIL — accepted requirements without test reference:")
        for req in enforced_missing:
            print(f"  {req.id}  [phase {req.phase_target}]  {req.path.relative_to(REPO)}")
        return 1

    print("OK — all in-phase accepted requirements have test references.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
