/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSPARSER_SEMA_H_
#define ZSPARSER_SEMA_H_

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
 * Semantic analyser for the zserio-c parser. Takes a parsed
 * AST UNIT and produces an analysed AST in-place by populating
 * the `resolved` side-channel on referencing nodes.
 *
 * The first slice covers symbol-table construction + name
 * resolution + duplicate-name detection. Expression typing,
 * constant evaluation, and packed-array eligibility analysis
 * land in follow-up slices.
 *
 * All allocation flows through the same arena that owns the
 * AST — analyser metadata has the same lifetime as the AST it
 * describes, so a single arena lifetime is the natural fit.
 */

typedef struct zsp_sema_binding {
    const uint8_t   *name;          /**< Points into source bytes (borrowed). */
    size_t           name_len;
    zsp_ast_node_t  *decl;          /**< Declaration node bound to the name. */
} zsp_sema_binding_t;

typedef struct zsp_sema_scope {
    struct zsp_sema_scope *parent;
    zsp_sema_binding_t    *bindings;
    size_t                 count;
    size_t                 cap;
} zsp_sema_scope_t;

typedef struct zsp_sema {
    const zsp_source_t   *source;
    zsp_arena_t          *arena;
    zsp_diag_collector_t *diag;
    zsp_sema_scope_t     *root;     /**< Top-level (file) scope. */
    zsp_sema_scope_t     *current;  /**< Tip of the scope stack. */
    zsp_file_id_t         current_file_id;  /**< Set during resolve to bias
                                                 same-file bindings on conflict. */
    bool                  current_file_valid;
} zsp_sema_t;

zserio_status_t zsp_sema_init(zsp_sema_t           *s,
                              const zsp_source_t   *src,
                              zsp_arena_t          *arena,
                              zsp_diag_collector_t *diag);

/**
 * Register an additional unit's top-level declarations into the
 * root scope. Used to make cross-file references resolvable —
 * the caller loads dependency files separately (e.g. via the
 * compilation driver) and registers each before analysing the
 * target unit.
 *
 * Returns ZSERIO_OK on success; duplicate-decl diagnostics
 * surface during the bind, same as `zsp_sema_analyse_unit`'s
 * own collection pass.
 */
zserio_status_t zsp_sema_register_unit(zsp_sema_t     *s,
                                       zsp_ast_node_t *unit);

/**
 * Resolve-only pass: assumes `zsp_sema_register_unit` has
 * already populated the root scope (possibly across multiple
 * files). Walks `unit` resolving TYPE_REF + EXPR_IDENT +
 * member-access, then runs the constant-evaluation pass.
 *
 * Used by the multi-file driver: register all units first to
 * build a combined symbol table, then resolve each unit
 * against it.
 */
zserio_status_t zsp_sema_resolve_unit(zsp_sema_t     *s,
                                      zsp_ast_node_t *unit);

/**
 * Analyse a parsed UNIT in place. Two passes:
 *   1. Collect every top-level declaration into the root scope.
 *      Duplicate names emit ZSP_SEV_ERROR diagnostics.
 *   2. Walk the AST and resolve every TYPE_REF / EXPR_IDENT
 *      against the scope chain. Unresolved references emit
 *      ZSP_SEV_ERROR. Successful resolutions populate
 *      `node->resolved`.
 *
 * Returns ZSERIO_OK whether or not diagnostics were emitted
 * (caller checks `zsp_diag_error_count`); returns
 * ZSERIO_ERR_OUT_OF_MEMORY only on allocator failure.
 */
zserio_status_t zsp_sema_analyse_unit(zsp_sema_t *s, zsp_ast_node_t *unit);

/* Lookup helpers exposed for tests and future passes. */

zsp_ast_node_t *zsp_sema_scope_find(const zsp_sema_scope_t *scope,
                                    const uint8_t *name, size_t len);

/* ----- Constant evaluation ----------------------------------- */

typedef enum zsp_const_kind {
    ZSP_CONST_UNKNOWN = 0,
    ZSP_CONST_UINT,
    ZSP_CONST_INT,
    ZSP_CONST_FLOAT,
    ZSP_CONST_BOOL,
    ZSP_CONST_STRING
} zsp_const_kind_t;

typedef struct zsp_sema_const {
    zsp_const_kind_t kind;
    union {
        uint64_t u;
        int64_t  i;
        double   f;
        bool     b;
        struct {
            const uint8_t *bytes;   /**< borrowed, into source */
            size_t         len;
        } s;
    } v;
} zsp_sema_const_t;

/**
 * Evaluate `expr` to a compile-time constant if possible.
 * Returns a pointer to the cached const-eval result on success,
 * NULL if the expression isn't a constant or refers to a
 * non-constant binding. Caches successful evaluations on
 * `expr->computed` so repeated calls are O(1).
 *
 * Cycle detection: setting `expr->computed` to the special
 * sentinel `zsp_sema_const_cycle_marker()` while in-flight; a
 * recursive visit observing the sentinel emits a diagnostic
 * and returns NULL.
 */
const zsp_sema_const_t *zsp_sema_eval(zsp_sema_t *s, zsp_ast_node_t *expr);

/** Sentinel used to detect cycles during const-eval. */
const zsp_sema_const_t *zsp_sema_const_cycle_marker(void);

/* ----- Template instantiation -------------------------------- */

/**
 * Scan the loaded units' AST for TYPE_REF nodes that supply
 * template arguments, then clone each used template into a
 * concrete instantiation (one per unique arg-list signature).
 * Each cloned decl is added to the templated decl's enclosing
 * unit's decls list. TYPE_REF nodes that drove an instantiation
 * are rewritten to point at the concrete clone.
 *
 * Called automatically as part of `zsp_sema_resolve_unit` and
 * `zsp_sema_analyse_unit` after name resolution completes.
 */
zserio_status_t zsp_sema_instantiate_templates(zsp_sema_t     *s,
                                               zsp_ast_node_t *unit);

#endif /* ZSPARSER_SEMA_H_ */
