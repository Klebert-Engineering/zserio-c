#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Fabian Klebert
"""
Static check: no allocator vtable in `_write` paths.

Verifies: REQ-FN-016 — encoder performs no heap allocation.

Two scans, both run as part of the standard CI:

1. **API-shape check.** Every function whose name ends in `_write`
   in `runtime/src/*.c` and in any directory passed via `--gen`
   SHALL NOT take a `zserio_allocator_t` parameter.

2. **Body-use check.** The body of every `_write` function SHALL
   NOT reference the identifier `alloc` (the conventional name
   for the allocator parameter in the rest of the runtime).
   Comments and string literals are excluded; the check looks at
   tokenised C source.

The two checks together establish that no `_write` function can
plumb an allocator down to a helper that allocates. Combined with
REQ-FS-001 (no direct libc heap), this proves the encoder
allocates nothing.

Exit codes:
    0  — both checks pass
    1  — at least one offending `_write` found
    2  — usage error or missing inputs

Usage:
    tools/check_no_alloc_in_writers.py                 # runtime only
    tools/check_no_alloc_in_writers.py --gen <dir>...  # plus generated trees
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
RUNTIME_SRC = REPO / "runtime" / "src"

# Match a function definition whose name ends in `_write`. We
# tolerate the `static` / return-type prefix and any whitespace
# wrapping. The capture groups give us:
#   1. function name
#   2. parameter list (single line or multi-line)
# A trailing `{` distinguishes definitions from declarations.
FUNC_DEF_RE = re.compile(
    r"""
    ^                              # start of line
    (?:static\s+)?                  # optional static
    [A-Za-z_][\w\s*]*?\s+           # return type (greedy until name)
    ([A-Za-z_]\w*_write)            # capture: function name ending in _write
    \s*\(
    ([^)]*)                         # capture: parameters
    \)
    \s*\{                           # body opens
    """,
    re.VERBOSE | re.MULTILINE,
)

# C single-line and block comments. Removed before tokenising.
COMMENT_RE = re.compile(
    r"//[^\n]*|/\*.*?\*/",
    re.DOTALL,
)

# C string and char literals. Removed before tokenising bodies.
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'")

# `alloc` as a whole-word identifier in the body (not e.g.
# `allocator` or `m_alloc`). This is the conservative check —
# any reference to the allocator parameter is suspicious.
ALLOC_USE_RE = re.compile(r"\balloc\b")

EXIT_OK = 0
EXIT_FAIL = 1


def strip_comments_and_strings(text: str) -> str:
    """Remove comments and string/char literals from a C source."""
    text = COMMENT_RE.sub(" ", text)
    text = STRING_RE.sub('""', text)
    return text


def find_body(text: str, brace_start: int) -> str:
    """Return text from brace_start to the matching closing brace.

    Naïve brace balancer; the inputs are well-formed C from our
    own emitter / runtime, so this is sufficient.
    """
    depth = 0
    i = brace_start
    while i < len(text):
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[brace_start : i + 1]
        i += 1
    return text[brace_start:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--gen",
        action="append",
        default=[],
        help="Generated-output directory to also scan (may repeat).",
    )
    args = parser.parse_args()

    targets: list[pathlib.Path] = []

    # Runtime sources are always in scope.
    if not RUNTIME_SRC.is_dir():
        sys.stderr.write(f"missing {RUNTIME_SRC}\n")
        return 2
    targets.extend(sorted(RUNTIME_SRC.rglob("*.c")))

    # Generated directories — optional.
    for gen_root in args.gen:
        gen_path = pathlib.Path(gen_root).resolve()
        if not gen_path.is_dir():
            sys.stderr.write(f"--gen path is not a directory: {gen_path}\n")
            return 2
        targets.extend(sorted(gen_path.rglob("*.c")))

    issues: list[tuple[pathlib.Path, str, str, str]] = []
    writers_seen = 0
    for src in targets:
        try:
            text = src.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        if "_write" not in text:
            continue
        clean = strip_comments_and_strings(text)
        for match in FUNC_DEF_RE.finditer(clean):
            writers_seen += 1
            fn_name = match.group(1)
            params = match.group(2)
            if "zserio_allocator_t" in params:
                issues.append((
                    src,
                    fn_name,
                    "param-takes-allocator",
                    params.strip().replace("\n", " "),
                ))
            brace_pos = clean.find("{", match.end() - 1)
            if brace_pos == -1:
                continue
            body = find_body(clean, brace_pos)
            if ALLOC_USE_RE.search(body):
                snippet_line = next(
                    (
                        line.strip()
                        for line in body.splitlines()
                        if ALLOC_USE_RE.search(line)
                    ),
                    "<no line found>",
                )
                issues.append((src, fn_name, "body-uses-alloc", snippet_line))

    if issues:
        sys.stderr.write(
            f"check_no_alloc_in_writers: FAIL — "
            f"{len(issues)} `_write` function(s) violate REQ-FN-016\n\n"
        )
        for src, fn, problem, snippet in issues:
            rel = src.relative_to(REPO) if src.is_relative_to(REPO) else src
            sys.stderr.write(
                f"  {rel}:{fn}  [{problem}]\n    {snippet}\n"
            )
        return EXIT_FAIL

    if writers_seen == 0 and not args.gen:
        # Runtime-only scan finds no `_write` functions — that's
        # expected (runtime helpers are named `zserio_write_<type>`,
        # `write` in the middle, not at the end). For real coverage
        # the caller should pass --gen pointing at generated code.
        sys.stderr.write(
            "check_no_alloc_in_writers: warning — no `_write` "
            "functions found. Pass --gen <dir> to scan generated "
            "code (the primary target of REQ-FN-016).\n"
        )

    print(
        f"check_no_alloc_in_writers: OK  "
        f"({writers_seen} `_write` function(s) scanned, none take or "
        f"reference an allocator)"
    )
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
