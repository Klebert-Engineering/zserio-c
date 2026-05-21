#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Klebert Engineering
"""
Version-consistency CI gate.

Cross-checks that the version numbers declared in
  - runtime/include/zserio/version.h
  - CMakeLists.txt (project() VERSION)
  - docs/spec/compatibility.md (Current versions table)
  - docs/spec/schema-language.md (Version: header)
  - docs/spec/wire-format.md (Version: header)

are mutually consistent. Exits non-zero with a diff-style report
on any disagreement; exits zero on success.

The script also serves as the in-CI existence proof for the
schema-language and wire-format spec documents — it parses both
files and would fail if either were absent. That existence is
what the spec-ownership requirements demand.

Verifies: REQ-DM-004 — schema-language reference is owned by zserio-c
                       (existence + version header of docs/spec/schema-language.md)
Verifies: REQ-DM-005 — wire-format reference is owned by zserio-c
                       (existence + version header of docs/spec/wire-format.md)
Verifies: REQ-DM-007 — versioning policy across the five axes
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

VERSION_H = REPO / "runtime/include/zserio/version.h"
CMAKE     = REPO / "CMakeLists.txt"
COMPAT    = REPO / "docs/spec/compatibility.md"
SCHEMA    = REPO / "docs/spec/schema-language.md"
WIRE      = REPO / "docs/spec/wire-format.md"


def fail(msg: str) -> None:
    sys.stderr.write(f"check_version_consistency: {msg}\n")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        fail(f"file not found: {path}")
        sys.exit(2)
    return path.read_text(encoding="utf-8")


def parse_version_h(text: str) -> dict[str, str]:
    """Extract release / wire / schema versions from version.h."""
    def grab(name: str) -> str:
        m = re.search(rf'#define\s+{name}\s+"([^"]+)"', text)
        if not m:
            fail(f"version.h: missing define {name}")
            sys.exit(2)
        return m.group(1)

    return {
        "release":         grab("ZSERIO_C_VERSION_STRING"),
        "wire_format":     grab("ZSERIO_WIRE_FORMAT_VERSION_STRING"),
        "schema_language": grab("ZSERIO_SCHEMA_LANGUAGE_VERSION_STRING"),
        "upstream_pin":    grab("ZSERIO_UPSTREAM_PIN_VERSION"),
    }


def parse_cmake(text: str) -> dict[str, str]:
    """Extract project VERSION and the wire/schema vars from CMakeLists.txt."""
    m_proj = re.search(
        r"project\s*\(\s*zserio-c\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
        text,
    )
    if not m_proj:
        fail("CMakeLists.txt: project(zserio-c VERSION x.y.z) not found")
        sys.exit(2)
    m_wire = re.search(r'set\(ZSERIO_WIRE_FORMAT_VERSION\s+"([^"]+)"\)', text)
    m_lang = re.search(r'set\(ZSERIO_SCHEMA_LANGUAGE_VERSION\s+"([^"]+)"\)', text)
    m_pin  = re.search(r'set\(ZSERIO_UPSTREAM_PIN\s+"([^"]+)"\)', text)
    if not (m_wire and m_lang and m_pin):
        fail("CMakeLists.txt: missing ZSERIO_WIRE_FORMAT_VERSION / "
             "ZSERIO_SCHEMA_LANGUAGE_VERSION / ZSERIO_UPSTREAM_PIN")
        sys.exit(2)
    return {
        "release":         m_proj.group(1),
        "wire_format":     m_wire.group(1),
        "schema_language": m_lang.group(1),
        "upstream_pin":    m_pin.group(1),
    }


def parse_compatibility_md(text: str) -> dict[str, str]:
    """Extract versions from the 'Current versions' table."""
    # The table has rows like:
    # | Release | `0.1.0` | ... |
    # | Wire format | `1.0` | ... |
    # | Schema language | `1.0` | ... |
    def row(label: str) -> str:
        m = re.search(
            rf"^\|\s*{re.escape(label)}\s*\|\s*`([^`]+)`\s*\|",
            text,
            re.MULTILINE,
        )
        if not m:
            fail(f"compatibility.md: row '{label}' not found in Current versions table")
            sys.exit(2)
        return m.group(1)

    return {
        "release":         row("Release"),
        "wire_format":     row("Wire format"),
        "schema_language": row("Schema language"),
    }


def parse_spec_header(path: pathlib.Path, text: str) -> str:
    """Extract the Version: NN.NN value from a spec doc header."""
    m = re.search(r"^\*\*Version:\*\*\s+([0-9]+\.[0-9]+)\b", text, re.MULTILINE)
    if not m:
        fail(f"{path.name}: missing '**Version:** N.N' line in header")
        sys.exit(2)
    return m.group(1)


def main() -> int:
    vh    = parse_version_h(read(VERSION_H))
    cmake = parse_cmake(read(CMAKE))
    cmd   = parse_compatibility_md(read(COMPAT))
    schema_hdr = parse_spec_header(SCHEMA, read(SCHEMA))
    wire_hdr   = parse_spec_header(WIRE, read(WIRE))

    sources = {
        "release": [
            ("version.h ZSERIO_C_VERSION_STRING",       vh["release"]),
            ("CMakeLists.txt project() VERSION",        cmake["release"]),
            ("compatibility.md Release row",            cmd["release"]),
        ],
        "wire_format": [
            ("version.h ZSERIO_WIRE_FORMAT_VERSION_STRING",   vh["wire_format"]),
            ("CMakeLists.txt ZSERIO_WIRE_FORMAT_VERSION",     cmake["wire_format"]),
            ("compatibility.md Wire format row",              cmd["wire_format"]),
            ("wire-format.md header Version: line",           wire_hdr),
        ],
        "schema_language": [
            ("version.h ZSERIO_SCHEMA_LANGUAGE_VERSION_STRING",   vh["schema_language"]),
            ("CMakeLists.txt ZSERIO_SCHEMA_LANGUAGE_VERSION",     cmake["schema_language"]),
            ("compatibility.md Schema language row",              cmd["schema_language"]),
            ("schema-language.md header Version: line",           schema_hdr),
        ],
        "upstream_pin": [
            ("version.h ZSERIO_UPSTREAM_PIN_VERSION", vh["upstream_pin"]),
            ("CMakeLists.txt ZSERIO_UPSTREAM_PIN",    cmake["upstream_pin"]),
        ],
    }

    failed = False
    for axis, entries in sources.items():
        values = {v for _, v in entries}
        if len(values) > 1:
            failed = True
            sys.stderr.write(f"\naxis '{axis}' is inconsistent:\n")
            for src, val in entries:
                sys.stderr.write(f"  {val:<12s}  ({src})\n")

    if failed:
        sys.stderr.write(
            "\ncheck_version_consistency: FAIL — fix the disagreeing source\n"
        )
        return 1

    print("check_version_consistency: OK")
    print(f"  release         : {vh['release']}")
    print(f"  wire format     : {vh['wire_format']}")
    print(f"  schema language : {vh['schema_language']}")
    print(f"  upstream pin    : {vh['upstream_pin']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
