/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSPARSER_PARSER_H_
#define ZSPARSER_PARSER_H_

#include "zserio/status.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/lexer.h"
#include "zsparser/source.h"

/**
 * @file
 * Recursive-descent parser entry point.
 *
 * Parse the source file pointed to by `file_id` (via `src`) into
 * an AST rooted at `ZSP_AST_UNIT`. All AST allocation flows
 * through `arena`; diagnostics flow through `diag`. The function
 * returns the AST even when diagnostics were emitted — callers
 * check `zsp_diag_error_count(diag) == 0` to decide acceptance.
 *
 * Returns ZSERIO_OK on success (even with diagnostics);
 * ZSERIO_ERR_INVALID_ARGUMENT on NULL inputs;
 * ZSERIO_ERR_OUT_OF_MEMORY on arena exhaustion.
 *
 * `*out_unit` is set to the unit node on success, or NULL on
 * failure. A unit with zero declarations is a valid result if
 * the input was empty / comments-only.
 */
zserio_status_t zsp_parse_unit(zsp_source_t          *src,
                               zsp_file_id_t          file_id,
                               zsp_arena_t           *arena,
                               zsp_diag_collector_t  *diag,
                               zsp_ast_node_t       **out_unit);

#endif /* ZSPARSER_PARSER_H_ */
