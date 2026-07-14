#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Klebert Engineering
"""
Extract one release's section from CHANGELOG.md (Keep a Changelog
format) for use as GitHub release notes.

Usage:
    tools/extract_changelog.py 0.1.0            # print the [0.1.0] section
    tools/extract_changelog.py v0.1.0           # leading 'v' is stripped

Exit codes:
    0 — section found and printed to stdout
    1 — no section for that version
    2 — usage error / missing changelog
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
CHANGELOG = REPO / "CHANGELOG.md"


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__ or "")
        return 2
    version = sys.argv[1].lstrip("vV")
    if not CHANGELOG.is_file():
        sys.stderr.write(f"missing {CHANGELOG}\n")
        return 2

    text = CHANGELOG.read_text(encoding="utf-8")
    # A section starts at '## [<version>]' and runs to the next
    # '## [' heading (or EOF). The heading line itself (with its
    # date) is excluded from the notes body.
    pattern = re.compile(
        r"^## \[" + re.escape(version) + r"\][^\n]*\n(.*?)(?=^## \[|\Z)",
        re.MULTILINE | re.DOTALL,
    )
    match = pattern.search(text)
    if match is None:
        sys.stderr.write(
            f"extract_changelog: no '## [{version}]' section in CHANGELOG.md\n"
        )
        return 1
    sys.stdout.write(match.group(1).strip() + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
