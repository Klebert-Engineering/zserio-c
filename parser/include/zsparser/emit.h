/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSPARSER_EMIT_H_
#define ZSPARSER_EMIT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zserio/status.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/source.h"

/**
 * @file
 * C code emitter for the zserio-c parser. Takes an analysed
 * AST UNIT (parsed by `zsp_parse_unit` and analysed by
 * `zsp_sema_analyse_unit`) and emits one `.h` + one `.c` file
 * per compound declaration plus a package-level header.
 *
 * Per Q3 (`docs/phase-7/scope.md`), this is a hand-written
 * tree-walking emitter — no template engine, no `<stdarg.h>`,
 * typed `emit_lit` / `emit_uint` / etc. helpers. Output is
 * delivered to a caller-supplied directory via a small file-
 * write callback so the emitter library carries no `<stdio.h>`
 * dependency itself.
 */

/**
 * File-write callback. The emitter invokes this once per
 * emitted file, passing the file's path (relative to the
 * caller-supplied root) and its complete contents.
 *
 * `userdata` is the value passed to `zsp_emit_unit`.
 *
 * Implementations write the bytes to disk or to an in-memory
 * map for testing. Returning false aborts the emit with
 * ZSERIO_ERR_OUT_OF_MEMORY (sticky failure mode).
 */
typedef bool (*zsp_emit_write_fn)(void       *userdata,
                                  const char *path,
                                  const char *bytes,
                                  size_t      len);

/**
 * Emit C code for the analysed UNIT. Outputs:
 *
 *   <package_path>/<TypeName>.h   — declarations
 *   <package_path>/<TypeName>.c   — implementations
 *
 * where `<package_path>` is the dotted package name with `.`
 * replaced by `/` (e.g. `geo.tile` → `geo/tile/`). One
 * (`.h`, `.c`) pair per top-level compound declaration (struct,
 * union, choice, enum, bitmask, subtype, const).
 *
 * Returns ZSERIO_OK on success, ZSERIO_ERR_INVALID_ARGUMENT
 * on NULL inputs, ZSERIO_ERR_OUT_OF_MEMORY if the write
 * callback fails or an internal buffer alloc fails.
 *
 * Diagnostics for unsupported language features land in `diag`
 * as ZSP_SEV_WARN; the emitter best-effort outputs what it can
 * and skips the rest.
 */
zserio_status_t zsp_emit_unit(const zsp_ast_node_t *unit,
                              zsp_arena_t          *arena,
                              zsp_diag_collector_t *diag,
                              const zsp_source_t   *source,
                              zsp_emit_write_fn     write,
                              void                 *userdata);

#endif /* ZSPARSER_EMIT_H_ */
