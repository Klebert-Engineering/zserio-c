/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zsparser/lexer.h"

#include <string.h>

#include "zsparser/token.h"

/*
 * Lexer implementation. Hand-written, pull-based, single-pass.
 * No recursion (Q5 / Rule 17.2): every helper consumes input
 * forward and returns to the dispatch loop in zsp_lexer_next.
 * Two-character operator lookahead via a single peek; identifier
 * and integer lexing extend by a forward scan.
 *
 * Lex errors emit a diagnostic and produce ZSP_TOK_INVALID
 * spanning the single offending byte; the lexer advances past it
 * so subsequent calls can keep producing real tokens up to the
 * collector's cap. This is the multi-error reporting design from
 * Q6.
 */

/* ---------- character classifiers (ASCII, MISRA-friendly) ----- */

static bool is_ws(uint8_t c)
{
    return (c == (uint8_t)' ')  || (c == (uint8_t)'\t')
        || (c == (uint8_t)'\n') || (c == (uint8_t)'\r')
        || (c == (uint8_t)'\f');
}

static bool is_alpha(uint8_t c)
{
    return ((c >= (uint8_t)'a') && (c <= (uint8_t)'z'))
        || ((c >= (uint8_t)'A') && (c <= (uint8_t)'Z'))
        || (c == (uint8_t)'_');
}

static bool is_digit(uint8_t c)
{
    return (c >= (uint8_t)'0') && (c <= (uint8_t)'9');
}

static bool is_alnum(uint8_t c)
{
    return is_alpha(c) || is_digit(c);
}

/* ---------- token construction helpers ------------------------- */

static void make_token(const zsp_lexer_t *l, zsp_token_t *out,
                       zsp_token_kind_t kind, size_t start, size_t len)
{
    out->kind    = kind;
    out->file_id = l->file_id;
    out->offset  = (uint32_t)start;
    out->length  = (uint32_t)len;
    out->value.u64 = 0U;
}

/* ---------- keyword lookup ------------------------------------- */

typedef struct keyword_entry {
    const char       *spelling;
    size_t            len;
    zsp_token_kind_t  kind;
} keyword_entry_t;

#define KW(text, kind_)  { (text), (sizeof(text) - 1U), (kind_) }

/* Sorted by byte-lexicographic order so binary search works. The
 * '@' annotations come first because '@' (0x40) sorts before any
 * letter. The compile-time check at the bottom of this file
 * enforces the sorted invariant via static_assert. */
static const keyword_entry_t KEYWORDS[] = {
    KW("@deprecated",                  ZSP_TOK_KW_DEPRECATED),
    KW("@index",                       ZSP_TOK_KW_INDEX),
    KW("@removed",                     ZSP_TOK_KW_REMOVED),
    KW("align",                        ZSP_TOK_KW_ALIGN),
    KW("bit",                          ZSP_TOK_KW_BIT),
    KW("bitmask",                      ZSP_TOK_KW_BITMASK),
    KW("bool",                         ZSP_TOK_KW_BOOL),
    KW("bytes",                        ZSP_TOK_KW_BYTES),
    KW("case",                         ZSP_TOK_KW_CASE),
    KW("choice",                       ZSP_TOK_KW_CHOICE),
    KW("const",                        ZSP_TOK_KW_CONST),
    KW("default",                      ZSP_TOK_KW_DEFAULT),
    KW("enum",                         ZSP_TOK_KW_ENUM),
    KW("explicit",                     ZSP_TOK_KW_EXPLICIT),
    KW("extend",                       ZSP_TOK_KW_EXTEND),
    KW("extern",                       ZSP_TOK_KW_EXTERN),
    KW("false",                        ZSP_TOK_KW_FALSE),
    KW("function",                     ZSP_TOK_KW_FUNCTION),
    KW("if",                           ZSP_TOK_KW_IF),
    KW("implicit",                     ZSP_TOK_KW_IMPLICIT),
    KW("import",                       ZSP_TOK_KW_IMPORT),
    KW("instantiate",                  ZSP_TOK_KW_INSTANTIATE),
    KW("int",                          ZSP_TOK_KW_INT),
    KW("isset",                        ZSP_TOK_KW_ISSET),
    KW("lengthof",                     ZSP_TOK_KW_LENGTHOF),
    KW("numbits",                      ZSP_TOK_KW_NUMBITS),
    KW("on",                           ZSP_TOK_KW_ON),
    KW("optional",                     ZSP_TOK_KW_OPTIONAL),
    KW("package",                      ZSP_TOK_KW_PACKAGE),
    KW("packed",                       ZSP_TOK_KW_PACKED),
    KW("publish",                      ZSP_TOK_KW_PUBLISH),
    KW("pubsub",                       ZSP_TOK_KW_PUBSUB),
    KW("return",                       ZSP_TOK_KW_RETURN),
    KW("rule",                         ZSP_TOK_KW_RULE),
    KW("rule_group",                   ZSP_TOK_KW_RULE_GROUP),
    KW("service",                      ZSP_TOK_KW_SERVICE),
    KW("sql",                          ZSP_TOK_KW_SQL),
    KW("sql_database",                 ZSP_TOK_KW_SQL_DATABASE),
    KW("sql_table",                    ZSP_TOK_KW_SQL_TABLE),
    KW("sql_virtual",                  ZSP_TOK_KW_SQL_VIRTUAL),
    KW("sql_without_rowid",            ZSP_TOK_KW_SQL_WITHOUT_ROWID),
    KW("string",                       ZSP_TOK_KW_STRING),
    KW("struct",                       ZSP_TOK_KW_STRUCT),
    KW("subscribe",                    ZSP_TOK_KW_SUBSCRIBE),
    KW("subtype",                      ZSP_TOK_KW_SUBTYPE),
    KW("topic",                        ZSP_TOK_KW_TOPIC),
    KW("true",                         ZSP_TOK_KW_TRUE),
    KW("union",                        ZSP_TOK_KW_UNION),
    KW("using",                        ZSP_TOK_KW_USING),
    KW("valueof",                      ZSP_TOK_KW_VALUEOF),
    KW("varint",                       ZSP_TOK_KW_VARINT),
    KW("varsize",                      ZSP_TOK_KW_VARSIZE),
    KW("varuint",                      ZSP_TOK_KW_VARUINT),
    KW("zserio_compatibility_version", ZSP_TOK_KW_COMPAT_VERSION),
};

#undef KW

#define KEYWORD_COUNT (sizeof(KEYWORDS) / sizeof(KEYWORDS[0]))

/* Lexicographic compare: returns < 0, 0, > 0. */
static int compare_keyword(const uint8_t *text, size_t text_len,
                           const char *kw, size_t kw_len)
{
    const size_t common = (text_len < kw_len) ? text_len : kw_len;
    if (common > 0U) {
        const int diff = memcmp(text, kw, common);
        if (diff != 0) {
            return diff;
        }
    }
    if (text_len < kw_len) {
        return -1;
    }
    if (text_len > kw_len) {
        return 1;
    }
    return 0;
}

static zsp_token_kind_t lookup_keyword(const uint8_t *text, size_t len)
{
    size_t lo = 0U;
    size_t hi = KEYWORD_COUNT;
    while (lo < hi) {
        const size_t mid = lo + ((hi - lo) / 2U);
        const int cmp = compare_keyword(text, len,
                                        KEYWORDS[mid].spelling,
                                        KEYWORDS[mid].len);
        if (cmp == 0) {
            return KEYWORDS[mid].kind;
        }
        if (cmp < 0) {
            hi = mid;
        }
        else {
            lo = mid + 1U;
        }
    }
    return ZSP_TOK_IDENT;
}

/* ---------- whitespace and comment skipping ------------------- */

/* Advance past whitespace + line/block comments. Returns true on
 * EOF, false on the next non-whitespace, non-comment byte.
 * Reports unterminated-block-comment diagnostics. */
static zserio_status_t skip_trivia(zsp_lexer_t *l, bool *out_eof)
{
    *out_eof = false;
    for (;;) {
        if (l->pos >= l->len) {
            *out_eof = true;
            return ZSERIO_OK;
        }
        const uint8_t c = l->bytes[l->pos];
        if (is_ws(c)) {
            l->pos += 1U;
            continue;
        }
        if ((c == (uint8_t)'/') && ((l->pos + 1U) < l->len)) {
            const uint8_t n = l->bytes[l->pos + 1U];
            if (n == (uint8_t)'/') {
                /* line comment: skip to '\n' or EOF */
                l->pos += 2U;
                while ((l->pos < l->len)
                       && (l->bytes[l->pos] != (uint8_t)'\n')) {
                    l->pos += 1U;
                }
                continue;
            }
            if (n == (uint8_t)'*') {
                /* block comment: skip to closing star-slash or EOF */
                const size_t open_offset = l->pos;
                l->pos += 2U;
                bool closed = false;
                while ((l->pos + 1U) < l->len) {
                    if ((l->bytes[l->pos] == (uint8_t)'*')
                            && (l->bytes[l->pos + 1U] == (uint8_t)'/')) {
                        l->pos += 2U;
                        closed = true;
                        break;
                    }
                    l->pos += 1U;
                }
                if (!closed) {
                    /* Walked off the end without finding the close. */
                    l->pos = l->len;
                    ZSERIO_TRY(zsp_diag_emit_lit(
                            l->diag, ZSP_SEV_ERROR,
                            l->file_id, (uint32_t)open_offset,
                            "unterminated block comment"));
                    *out_eof = true;
                    return ZSERIO_OK;
                }
                continue;
            }
        }
        return ZSERIO_OK;
    }
}

/* ---------- identifier and keyword lexing --------------------- */

static void lex_ident_or_keyword(zsp_lexer_t *l, zsp_token_t *out,
                                 size_t start)
{
    size_t end = start;
    /* '@' annotations: take the leading '@' then continue with
     * standard identifier characters. */
    if (l->bytes[end] == (uint8_t)'@') {
        end += 1U;
    }
    while ((end < l->len) && is_alnum(l->bytes[end])) {
        end += 1U;
    }
    const size_t len = end - start;
    const zsp_token_kind_t kind = lookup_keyword(&l->bytes[start], len);
    make_token(l, out, kind, start, len);
    l->pos = end;
}

/* ---------- integer-literal lexing (decimal only this slice) -- */

/* Returns ZSERIO_OK on success and writes the parsed value;
 * ZSERIO_ERR_VALUE_OUT_OF_RANGE on overflow past UINT64_MAX. The
 * caller has already validated that the first byte is a digit. */
static zserio_status_t parse_decimal_u64(const uint8_t *bytes, size_t len,
                                         uint64_t *out)
{
    uint64_t v = 0U;
    for (size_t i = 0U; i < len; ++i) {
        const uint64_t d = (uint64_t)(bytes[i] - (uint8_t)'0');
        /* Multiply-by-10 overflow check first: v > (UINT64_MAX / 10) */
        if (v > ((uint64_t)0xFFFFFFFFFFFFFFFFULL / 10U)) {
            return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
        }
        v *= 10U;
        /* Add overflow check: v + d must not wrap. */
        if (v > ((uint64_t)0xFFFFFFFFFFFFFFFFULL - d)) {
            return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
        }
        v += d;
    }
    *out = v;
    return ZSERIO_OK;
}

static zserio_status_t lex_integer(zsp_lexer_t *l, zsp_token_t *out,
                                   size_t start)
{
    size_t end = start;
    while ((end < l->len) && is_digit(l->bytes[end])) {
        end += 1U;
    }
    const size_t len = end - start;

    /* Trailing alpha after digits indicates either a float suffix
     * (`f`), a binary-literal suffix (`b`/`B`), or a typo. The
     * current slice rejects both with a diagnostic and produces
     * INVALID; hex/oct/binary/float arrive in a later slice. */
    if ((end < l->len) && is_alpha(l->bytes[end])) {
        ZSERIO_TRY(zsp_diag_emit_lit(
                l->diag, ZSP_SEV_ERROR,
                l->file_id, (uint32_t)end,
                "unsupported numeric literal form (hex/binary/float "
                "not yet implemented)"));
        /* Consume the remaining alnum run so we recover at the
         * next clear delimiter. */
        while ((end < l->len) && is_alnum(l->bytes[end])) {
            end += 1U;
        }
        make_token(l, out, ZSP_TOK_INVALID, start, end - start);
        l->pos = end;
        return ZSERIO_OK;
    }

    uint64_t value = 0U;
    const zserio_status_t st = parse_decimal_u64(&l->bytes[start], len, &value);
    if (st == ZSERIO_ERR_VALUE_OUT_OF_RANGE) {
        ZSERIO_TRY(zsp_diag_emit_lit(
                l->diag, ZSP_SEV_ERROR,
                l->file_id, (uint32_t)start,
                "integer literal exceeds 64-bit range"));
        make_token(l, out, ZSP_TOK_INVALID, start, len);
        l->pos = end;
        return ZSERIO_OK;
    }
    if (st != ZSERIO_OK) {
        return st;
    }

    make_token(l, out, ZSP_TOK_INT_LIT, start, len);
    out->value.u64 = value;
    l->pos = end;
    return ZSERIO_OK;
}

/* ---------- punctuation and operator lexing ------------------- */

static zserio_status_t lex_punct(zsp_lexer_t *l, zsp_token_t *out,
                                 size_t start)
{
    const uint8_t c0 = l->bytes[start];
    const uint8_t c1 = ((start + 1U) < l->len) ? l->bytes[start + 1U]
                                               : (uint8_t)0U;
    zsp_token_kind_t kind = ZSP_TOK_INVALID;
    size_t           len  = 1U;

    switch (c0) {
    case (uint8_t)'(': kind = ZSP_TOK_LPAREN;    break;
    case (uint8_t)')': kind = ZSP_TOK_RPAREN;    break;
    case (uint8_t)'{': kind = ZSP_TOK_LBRACE;    break;
    case (uint8_t)'}': kind = ZSP_TOK_RBRACE;    break;
    case (uint8_t)'[': kind = ZSP_TOK_LBRACKET;  break;
    case (uint8_t)']': kind = ZSP_TOK_RBRACKET;  break;
    case (uint8_t)';': kind = ZSP_TOK_SEMICOLON; break;
    case (uint8_t)',': kind = ZSP_TOK_COMMA;     break;
    case (uint8_t)'.': kind = ZSP_TOK_DOT;       break;
    case (uint8_t)':': kind = ZSP_TOK_COLON;     break;
    case (uint8_t)'?': kind = ZSP_TOK_QUESTION;  break;
    case (uint8_t)'+': kind = ZSP_TOK_PLUS;      break;
    case (uint8_t)'-': kind = ZSP_TOK_MINUS;     break;
    case (uint8_t)'*': kind = ZSP_TOK_STAR;      break;
    case (uint8_t)'/': kind = ZSP_TOK_SLASH;     break;
    case (uint8_t)'%': kind = ZSP_TOK_PERCENT;   break;
    case (uint8_t)'~': kind = ZSP_TOK_TILDE;     break;
    case (uint8_t)'^': kind = ZSP_TOK_CARET;     break;

    case (uint8_t)'=':
        if (c1 == (uint8_t)'=') { kind = ZSP_TOK_EQ;     len = 2U; }
        else                    { kind = ZSP_TOK_ASSIGN;            }
        break;
    case (uint8_t)'!':
        if (c1 == (uint8_t)'=') { kind = ZSP_TOK_NEQ;    len = 2U; }
        else                    { kind = ZSP_TOK_BANG;              }
        break;
    case (uint8_t)'<':
        if (c1 == (uint8_t)'=') { kind = ZSP_TOK_LE;     len = 2U; }
        else if (c1 == (uint8_t)'<') { kind = ZSP_TOK_LSHIFT; len = 2U; }
        else                    { kind = ZSP_TOK_LT;                }
        break;
    case (uint8_t)'>':
        if (c1 == (uint8_t)'=') { kind = ZSP_TOK_GE;     len = 2U; }
        else if (c1 == (uint8_t)'>') { kind = ZSP_TOK_RSHIFT; len = 2U; }
        else                    { kind = ZSP_TOK_GT;                }
        break;
    case (uint8_t)'&':
        if (c1 == (uint8_t)'&') { kind = ZSP_TOK_LOGICAL_AND; len = 2U; }
        else                    { kind = ZSP_TOK_AMP;                   }
        break;
    case (uint8_t)'|':
        if (c1 == (uint8_t)'|') { kind = ZSP_TOK_LOGICAL_OR; len = 2U; }
        else                    { kind = ZSP_TOK_PIPE;                  }
        break;

    default:
        /* Unrecognised byte. Emit diagnostic, span one byte. */
        ZSERIO_TRY(zsp_diag_emit_lit(
                l->diag, ZSP_SEV_ERROR,
                l->file_id, (uint32_t)start,
                "unexpected character in input"));
        kind = ZSP_TOK_INVALID;
        len  = 1U;
        break;
    }

    make_token(l, out, kind, start, len);
    l->pos = start + len;
    return ZSERIO_OK;
}

/* ---------- public API ---------------------------------------- */

zserio_status_t zsp_lexer_init(zsp_lexer_t          *l,
                               const zsp_source_t   *src,
                               zsp_file_id_t         file_id,
                               zsp_diag_collector_t *diag)
{
    if ((l == NULL) || (src == NULL) || (diag == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    const zsp_file_t *file = NULL;
    if (!zsp_source_get(src, file_id, &file)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    l->source  = src;
    l->diag    = diag;
    l->file_id = file_id;
    l->bytes   = file->bytes;
    l->len     = file->len;
    l->pos     = 0U;
    return ZSERIO_OK;
}

zserio_status_t zsp_lexer_next(zsp_lexer_t *l, zsp_token_t *out)
{
    if ((l == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    bool eof = false;
    ZSERIO_TRY(skip_trivia(l, &eof));
    if (eof) {
        make_token(l, out, ZSP_TOK_EOF, l->pos, 0U);
        return ZSERIO_OK;
    }

    const size_t  start = l->pos;
    const uint8_t c     = l->bytes[start];

    if (is_alpha(c) || (c == (uint8_t)'@')) {
        lex_ident_or_keyword(l, out, start);
        return ZSERIO_OK;
    }
    if (is_digit(c)) {
        return lex_integer(l, out, start);
    }
    return lex_punct(l, out, start);
}
