/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSPARSER_LEXER_H_
#define ZSPARSER_LEXER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zserio/status.h"

#include "zsparser/diag.h"
#include "zsparser/source.h"
#include "zsparser/token.h"

/**
 * @file
 * Pull-based lexer for the zserio schema language.
 *
 * One token per `zsp_lexer_next` call. The parser (a future
 * slice) wraps this in its own one-token or two-token lookahead
 * buffer; nothing in the lexer maintains lookahead state itself.
 *
 * After EOF the lexer keeps returning ZSP_TOK_EOF — no error,
 * no advance. This is the parser-convenience contract that
 * removes a class of "did we already finish" checks at the
 * recursive-descent call sites.
 *
 * Lex errors (invalid characters, unterminated string literals,
 * out-of-range integer literals) are reported via the supplied
 * `zsp_diag_collector_t` and produce a ZSP_TOK_INVALID token.
 * The lexer recovers by advancing past the offending byte; the
 * parser may continue and surface further errors until the
 * collector's cap (Q6 default 20) is hit.
 */

typedef struct zsp_lexer {
    const zsp_source_t   *source;
    zsp_diag_collector_t *diag;
    zsp_file_id_t         file_id;
    const uint8_t        *bytes;   /**< Shortcut to source file's bytes. */
    size_t                len;     /**< Source-file byte count. */
    size_t                pos;     /**< Current byte offset. */
} zsp_lexer_t;

/**
 * Initialise a lexer over the given source file. `src` and
 * `diag` MUST outlive the lexer. `file_id` MUST refer to a
 * registered file; `ZSERIO_ERR_INVALID_ARGUMENT` is returned
 * otherwise.
 *
 * The lexer holds the source manager as `const`: it reads file
 * bytes via `zsp_source_get` but does not trigger line-start
 * table builds at lex time. Line/col recovery happens at
 * diagnostic-formatting time on a non-const source pointer
 * supplied to the formatter.
 */
zserio_status_t zsp_lexer_init(zsp_lexer_t          *l,
                               const zsp_source_t   *src,
                               zsp_file_id_t         file_id,
                               zsp_diag_collector_t *diag);

/**
 * Produce the next token. `*out` is fully overwritten; the
 * previous contents are not consulted. Returns
 * ZSERIO_ERR_INVALID_ARGUMENT on NULL inputs;
 * ZSERIO_ERR_OUT_OF_MEMORY only if a lex-time diagnostic
 * needed a copied message and the allocator failed (lex-only
 * messages today are all literals, so this is currently
 * unreachable — preserved as part of the API contract so
 * callers handle it once we introduce diagnostics with
 * interpolated content).
 *
 * Otherwise returns ZSERIO_OK; the `*out` token may be
 * ZSP_TOK_EOF or ZSP_TOK_INVALID depending on the input.
 */
zserio_status_t zsp_lexer_next(zsp_lexer_t *l, zsp_token_t *out);

#endif /* ZSPARSER_LEXER_H_ */
