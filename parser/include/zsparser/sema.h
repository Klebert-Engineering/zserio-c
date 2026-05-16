/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
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
} zsp_sema_t;

zserio_status_t zsp_sema_init(zsp_sema_t           *s,
                              const zsp_source_t   *src,
                              zsp_arena_t          *arena,
                              zsp_diag_collector_t *diag);

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

#endif /* ZSPARSER_SEMA_H_ */
