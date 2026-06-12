/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSPARSER_SESSION_H_
#define ZSPARSER_SESSION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zserio/status.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/sema.h"
#include "zsparser/source.h"

/**
 * @file
 * Multi-file compilation session: loads `.zs` files from disk
 * along with their transitively imported dependencies, parses
 * them into ASTs, and analyzes the combined symbol set against
 * a shared root scope.
 *
 * Import-path resolution: a dotted package name `a.b.c` is
 * looked up as `<root>/a/b/c.zs` across each configured source
 * root (in order). If the wildcard form `import a.b.c.*;` is
 * used the same path is tried. Single-symbol imports
 * `import a.b.c.Foo;` first try `<root>/a/b/c/Foo.zs`, then
 * fall back to `<root>/a/b/c.zs` (the conventional layout
 * where `c.zs` declares `package a.b.c` and defines `Foo` as
 * a top-level symbol).
 *
 * Cycles in the import graph are handled by deduplicating
 * already-loaded paths.
 */

typedef struct zsp_session {
    zserio_allocator_t   alloc;
    zsp_source_t         source;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_sema_t           sema;

    /* Source-root directories searched in order during import
     * resolution. Borrowed (caller-owned) — must outlive the
     * session. */
    const char *const   *roots;
    size_t               n_roots;

    /* Per-file metadata indexed by file_id (== position in
     * source manager). */
    struct zsp_session_file {
        zsp_ast_node_t  *unit;
        char            *path;       /* malloc'd, owned by session */
    } *files;
    size_t               n_files;
    size_t               files_cap;
    bool                 sema_inited;
} zsp_session_t;

/**
 * Initialize a session with the given source roots. The roots
 * array is borrowed — caller must keep the strings alive for
 * the lifetime of the session.
 *
 * `n_roots` may be 0; in that case only absolute paths or
 * paths relative to the process CWD will resolve.
 */
zserio_status_t zsp_session_init(zsp_session_t *s,
                                 const char *const *roots,
                                 size_t n_roots);

void zsp_session_dispose(zsp_session_t *s);

/**
 * Load a file by absolute or root-relative path. Parses it,
 * recursively follows its `import` declarations to load
 * dependency files, and stores the result.
 *
 * `*out_unit` is set to the parsed AST UNIT on success (or
 * NULL if loading failed).
 */
zserio_status_t zsp_session_load(zsp_session_t   *s,
                                 const char      *path,
                                 zsp_ast_node_t **out_unit);

/**
 * Analyze all loaded units. Registers every unit's top-level
 * declarations into a shared root scope, then runs name
 * resolution + constant evaluation on each unit.
 *
 * Idempotent: calling twice on the same session is a no-op.
 */
zserio_status_t zsp_session_analyze(zsp_session_t *s);

/**
 * Prepend the dotted `prefix` (e.g. "index_workaround" or
 * "x.y.z") to every loaded unit's `package` declaration. Mirrors
 * upstream zserio's `-setTopLevelPackage` CLI flag.
 *
 * Mutates the AST in place; MUST be called between
 * `zsp_session_load` and `zsp_session_analyze`.
 *
 * Returns `ZSERIO_OK` on success, `ZSERIO_ERR_INVALID_ARGUMENT`
 * if `s` is NULL, `ZSERIO_ERR_OUT_OF_MEMORY` on allocation
 * failure. An empty or NULL `prefix` is a no-op and returns OK.
 */
zserio_status_t zsp_session_set_top_level_package(zsp_session_t *s,
                                                  const char    *prefix);

#endif /* ZSPARSER_SESSION_H_ */
