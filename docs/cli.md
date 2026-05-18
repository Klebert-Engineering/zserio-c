# `zserio_c_gen` — CLI reference

The C-native parser/generator. Replaces the Java
`ZserioTool -c <ext>` invocation on the C target.

## Synopsis

```
zserio_c_gen <input.zs> -c <output-dir> [-src <root>...]
                        [-setTopLevelPackage <pkg>]
```

## Options

### `<input.zs>` — top-level schema

The schema file to compile. Its `import` graph is followed
transitively — you don't need to list dependencies separately.
Imports are resolved against `-src` roots (or, if none are given,
against the directory containing the input).

The input must be a `.zs` file. The generator does not read from
stdin.

### `-c <output-dir>` — output directory

Where to emit generated `.c` and `.h` files. The directory is
created if it doesn't exist. Sub-directories mirror the package
path of the emitted types — a schema declaring
`package foo.bar.baz;` emits into `<output-dir>/foo/bar/baz/`.

The generator does not modify files outside `-c <output-dir>`.

Re-running with the same inputs produces byte-identical output. If
your build system needs a dependency stamp file, touch one after
the gen command — there is no separate `--depfile` flag yet.

### `-src <root>` — import search root

Import resolution path. May be repeated; roots are searched in
order. Defaults to the directory containing `<input.zs>` if no
`-src` is supplied.

A schema importing `import a.b.c.*;` is resolved as
`<root>/a/b/c.zs` against each root. The wildcard variant follows
the upstream convention of one package per file.

```
-src schemas/                 # ./schemas/a/b/c.zs
-src schemas/ -src vendor/    # try ./schemas/ first, then ./vendor/
```

### `-setTopLevelPackage <pkg>` — package prefix

Prepend `pkg` (a dotted name) to every loaded unit's `package`
declaration. Mirrors upstream zserio's flag of the same name.

The use case is identifier collisions. A schema declaring
`package index;` produces code where some symbol or namespace
clashes with libc's `index()` (pulled in by `<strings.h>` on the
cpp generator's side). Re-running with
`-setTopLevelPackage index_workaround` moves everything under
`index_workaround.index.*` and resolves the clash.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success. Files written under `-c <output-dir>`. |
| 1 | Internal error (emit failed, file write failed, OOM). |
| 2 | Usage error (missing arguments, bad flag combination). |

Parse and semantic diagnostics are printed to stderr in GCC
format (`<file>:<line>:<col>: error: <message>`) and exit with 1.

## Behavior

- **Deterministic** — same inputs, same outputs across runs.
- **All-or-nothing** — on emit failure, partially-written files
  may exist under `-c <output-dir>`; do not consume them. A
  successful invocation overwrites everything cleanly.
- **No network access**. No external tool spawning. Reads files
  under `-src` roots only.
- **Idempotent re-runs** — safe to re-run on the same output
  directory.

## Examples

### Simple schema

```sh
zserio_c_gen schemas/hello.zs -c build/zserio-out
```

Imports resolve against `schemas/` (the input file's directory).

### Multi-root project

```sh
zserio_c_gen app/schemas/main.zs \
    -c build/zserio-out \
    -src app/schemas \
    -src vendor/upstream/schemas
```

`main.zs`'s imports are looked up first under `app/schemas/`,
then under `vendor/upstream/schemas/`.

### Package-prefix workaround

```sh
zserio_c_gen packages/zs/index.zs \
    -c build/zserio-out \
    -src packages/zs \
    -setTopLevelPackage index_workaround
```

The schema's `package index;` is rewritten to
`package index_workaround.index;` before generation. Emitted
identifiers and file paths use the prefixed form.

## See also

- [Build integration](user-guide/build-integration.md) — wiring
  the generator step into CMake / Make.
- [Generated code](user-guide/generated-code.md) — what comes out
  the other side.
- [Contributing](contributing.md) — building and testing the
  generator itself.
