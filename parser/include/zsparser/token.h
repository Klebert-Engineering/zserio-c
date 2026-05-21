/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSPARSER_TOKEN_H_
#define ZSPARSER_TOKEN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zsparser/source.h"

/**
 * @file
 * Token model for the zserio-c lexer + parser.
 *
 * Tokens carry the `(file_id, offset, length)` triple that lets
 * any later consumer recover both the literal source span (via
 * the source manager's bytes) and the (line, col) position (via
 * the source manager's lazy line-start table). This is the same
 * locator shape the AST uses (Q6 decision in
 * `docs/phase-7/scope.md`); tokens are simply locators stamped
 * with a kind.
 *
 * Atomic-literal payloads (integers, floats) are decoded by the
 * lexer and stored in the token's `value` union; identifier and
 * keyword spelling is NOT stored — callers reconstruct it from
 * `(file_id, offset, length)` when needed. This keeps every
 * token at a fixed compact size (16 bytes on 64-bit hosts).
 *
 * Adding a token kind requires:
 *   1. New enumerator below.
 *   2. Entry in the keyword table (if it's a keyword) in lexer.c.
 *   3. New case in `zsp_token_kind_name` (token.c) for diagnostics.
 * The default-trap switch pattern catches missing cases at
 * compile time under our `-Wswitch-enum` flags.
 */

typedef enum zsp_token_kind {
    /* Terminal markers. */
    ZSP_TOK_EOF = 0,
    ZSP_TOK_INVALID,

    /* Atoms. */
    ZSP_TOK_IDENT,
    ZSP_TOK_INT_LIT,
    ZSP_TOK_FLOAT_LIT,
    ZSP_TOK_STRING_LIT,

    /* Keywords. Kept contiguous so `zsp_token_is_keyword` is a
     * simple range check. The exact ordering within the band
     * does not matter for that check — it does match the
     * sorted-by-spelling order the keyword lookup table uses
     * for binary search, for cache-friendliness. */
    ZSP_TOK_KW_ALIGN,
    ZSP_TOK_KW_BIT,                  /**< 'bit' — bitfield prefix in `bit:N`. */
    ZSP_TOK_KW_BITMASK,
    ZSP_TOK_KW_BOOL,
    ZSP_TOK_KW_BYTES,
    ZSP_TOK_KW_CASE,
    ZSP_TOK_KW_CHOICE,
    ZSP_TOK_KW_COMPAT_VERSION,       /**< 'zserio_compatibility_version'. */
    ZSP_TOK_KW_CONST,
    ZSP_TOK_KW_DEFAULT,
    ZSP_TOK_KW_DEPRECATED,           /**< '@deprecated'. */
    ZSP_TOK_KW_ENUM,
    ZSP_TOK_KW_EXPLICIT,
    ZSP_TOK_KW_EXTEND,
    ZSP_TOK_KW_EXTERN,
    ZSP_TOK_KW_FALSE,
    ZSP_TOK_KW_FUNCTION,
    ZSP_TOK_KW_IF,
    ZSP_TOK_KW_IMPLICIT,
    ZSP_TOK_KW_IMPORT,
    ZSP_TOK_KW_INDEX,                /**< '@index'. */
    ZSP_TOK_KW_INSTANTIATE,
    ZSP_TOK_KW_INT,                  /**< 'int' — bitfield prefix in `int:N`. */
    ZSP_TOK_KW_ISSET,
    ZSP_TOK_KW_LENGTHOF,
    ZSP_TOK_KW_NUMBITS,
    ZSP_TOK_KW_ON,
    ZSP_TOK_KW_OPTIONAL,
    ZSP_TOK_KW_PACKAGE,
    ZSP_TOK_KW_PACKED,
    ZSP_TOK_KW_PUBLISH,
    ZSP_TOK_KW_PUBSUB,
    ZSP_TOK_KW_REMOVED,              /**< '@removed'. */
    ZSP_TOK_KW_RETURN,
    ZSP_TOK_KW_RULE,
    ZSP_TOK_KW_RULE_GROUP,
    ZSP_TOK_KW_SERVICE,
    ZSP_TOK_KW_SQL,
    ZSP_TOK_KW_SQL_DATABASE,
    ZSP_TOK_KW_SQL_TABLE,
    ZSP_TOK_KW_SQL_VIRTUAL,
    ZSP_TOK_KW_SQL_WITHOUT_ROWID,
    ZSP_TOK_KW_STRING,
    ZSP_TOK_KW_STRUCT,
    ZSP_TOK_KW_SUBSCRIBE,
    ZSP_TOK_KW_SUBTYPE,
    ZSP_TOK_KW_TOPIC,
    ZSP_TOK_KW_TRUE,
    ZSP_TOK_KW_UNION,
    ZSP_TOK_KW_USING,
    ZSP_TOK_KW_VALUEOF,
    ZSP_TOK_KW_VARINT,
    ZSP_TOK_KW_VARSIZE,
    ZSP_TOK_KW_VARUINT,

    /* Punctuation. */
    ZSP_TOK_LPAREN,           /**< ( */
    ZSP_TOK_RPAREN,           /**< ) */
    ZSP_TOK_LBRACE,           /**< { */
    ZSP_TOK_RBRACE,           /**< } */
    ZSP_TOK_LBRACKET,         /**< [ */
    ZSP_TOK_RBRACKET,         /**< ] */
    ZSP_TOK_SEMICOLON,        /**< ; */
    ZSP_TOK_COMMA,            /**< , */
    ZSP_TOK_DOT,              /**< . */
    ZSP_TOK_COLON,            /**< : */
    ZSP_TOK_QUESTION,         /**< ? */

    /* Operators. */
    ZSP_TOK_ASSIGN,           /**< = */
    ZSP_TOK_EQ,               /**< == */
    ZSP_TOK_NEQ,              /**< != */
    ZSP_TOK_LT,               /**< < */
    ZSP_TOK_GT,               /**< > */
    ZSP_TOK_LE,               /**< <= */
    ZSP_TOK_GE,               /**< >= */
    ZSP_TOK_PLUS,             /**< + */
    ZSP_TOK_MINUS,            /**< - */
    ZSP_TOK_STAR,             /**< * */
    ZSP_TOK_SLASH,            /**< / */
    ZSP_TOK_PERCENT,          /**< % */
    ZSP_TOK_BANG,             /**< ! */
    ZSP_TOK_TILDE,            /**< ~ */
    ZSP_TOK_AMP,              /**< & */
    ZSP_TOK_PIPE,             /**< | */
    ZSP_TOK_CARET,            /**< ^ */
    ZSP_TOK_LOGICAL_AND,      /**< && */
    ZSP_TOK_LOGICAL_OR,       /**< || */
    ZSP_TOK_LSHIFT,           /**< << */
    ZSP_TOK_RSHIFT,           /**< >> */

    /* Sentinel: one past the last valid kind. Use for bounds
     * checks; do NOT pass this value as a real token kind. */
    ZSP_TOK_KIND_COUNT
} zsp_token_kind_t;

/**
 * Tokens are the parser's atomic input unit. The struct is
 * sized to fit in a single cache line on 64-bit hosts (24 bytes
 * total, padded to 32 after alignment).
 *
 * `length` counts the source bytes covered by this token. For
 * atomic literals the source span includes any trailing suffix
 * (the 'f' of a float literal, the 'b' of a binary integer
 * literal). For string literals the span includes the enclosing
 * quotes.
 */
typedef struct zsp_token {
    zsp_token_kind_t kind;
    zsp_file_id_t    file_id;
    uint32_t         offset;
    uint32_t         length;
    union {
        uint64_t u64;        /**< Decoded integer literal (INT_LIT). */
        double   f64;        /**< Decoded float literal (FLOAT_LIT). */
    } value;
} zsp_token_t;

/** Returns true if `k` names a keyword token (`ZSP_TOK_KW_*`). */
bool zsp_token_is_keyword(zsp_token_kind_t k);

/**
 * Returns a static spelling for `k` suitable for error messages.
 * For keywords: the keyword text (e.g., `"struct"`). For
 * punctuation/operators: the operator spelling (e.g., `"=="`).
 * For atoms: a placeholder (`"identifier"`, `"integer literal"`,
 * etc.). For ZSP_TOK_KIND_COUNT and any out-of-range value:
 * `"<invalid-token-kind>"`.
 *
 * The returned pointer has static lifetime; callers must NOT
 * free it.
 */
const char *zsp_token_kind_name(zsp_token_kind_t k);

#endif /* ZSPARSER_TOKEN_H_ */
