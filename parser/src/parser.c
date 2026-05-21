/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include "zsparser/parser.h"

#include <string.h>

#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/lexer.h"
#include "zsparser/token.h"

/*
 * Recursive-descent parser. One-token lookahead via `cur` + `peek`.
 * Per Q5 (`docs/phase-7/scope.md`), the recursion deviation (Rule
 * 17.2) is bounded by the grammar's static maximum nesting depth
 * and closed by the stack-budget gate (REQ-FS-003).
 *
 * Pratt expression parser for binary/ternary; precedence table at
 * the top of the expression section. Error recovery is the
 * classic panic-mode strategy: on error, skip tokens until a
 * synchronization sentinel (typically `;` or `}` or a top-level
 * keyword).
 *
 * AST construction is allocator-disciplined: nodes via
 * `zsp_ast_new`, lists via `zsp_ast_list_t` which finalises into
 * arena-allocated arrays.
 */

/* ============================================================ */
/* Parser state                                                  */
/* ============================================================ */

typedef struct parser {
    zsp_lexer_t           lexer;
    zsp_arena_t          *arena;
    zsp_diag_collector_t *diag;
    zsp_file_id_t         file_id;
    zsp_token_t           cur;
    zsp_token_t           peek;
    /* Single-slot pushback for `>>` splits: when a nested
     * template close needs to decompose RSHIFT into two `>`,
     * the original peek is stashed here and consumed on the
     * next advance. */
    zsp_token_t           stash;
    bool                  has_stash;
    bool                  oom;        /**< Sticky: arena ran out. */
} parser_t;

static zserio_status_t parser_init(parser_t *p,
                                   zsp_source_t *src, zsp_file_id_t file_id,
                                   zsp_arena_t *arena, zsp_diag_collector_t *diag)
{
    p->arena     = arena;
    p->diag      = diag;
    p->file_id   = file_id;
    p->oom       = false;
    p->has_stash = false;
    ZSERIO_TRY(zsp_lexer_init(&p->lexer, src, file_id, diag));
    ZSERIO_TRY(zsp_lexer_next(&p->lexer, &p->cur));
    ZSERIO_TRY(zsp_lexer_next(&p->lexer, &p->peek));
    return ZSERIO_OK;
}

/* ============================================================ */
/* Token-stream helpers                                          */
/* ============================================================ */

static void advance(parser_t *p)
{
    p->cur = p->peek;
    if (p->has_stash) {
        p->peek      = p->stash;
        p->has_stash = false;
    }
    else {
        (void)zsp_lexer_next(&p->lexer, &p->peek);
    }
}

static bool check(const parser_t *p, zsp_token_kind_t k)
{
    return p->cur.kind == k;
}

static bool eat(parser_t *p, zsp_token_kind_t k)
{
    if (p->cur.kind == k) {
        advance(p);
        return true;
    }
    return false;
}

static void emit_error_at_cur(parser_t *p, const char *msg)
{
    (void)zsp_diag_emit_lit(p->diag, ZSP_SEV_ERROR,
                            p->cur.file_id, p->cur.offset, msg);
}

/* ----- Speculative-parse snapshot/restore --------------------- */

typedef struct parser_snapshot {
    size_t      lexer_pos;
    zsp_token_t cur;
    zsp_token_t peek;
    size_t      diag_count;
    size_t      diag_error_count;
    bool        diag_truncated;
    bool        oom;
} parser_snapshot_t;

static parser_snapshot_t save_state(const parser_t *p)
{
    parser_snapshot_t s;
    s.lexer_pos        = p->lexer.pos;
    s.cur              = p->cur;
    s.peek             = p->peek;
    s.diag_count       = p->diag->count;
    s.diag_error_count = p->diag->error_count;
    s.diag_truncated   = p->diag->truncated;
    s.oom              = p->oom;
    return s;
}

static void restore_state(parser_t *p, parser_snapshot_t s)
{
    p->lexer.pos        = s.lexer_pos;
    p->cur              = s.cur;
    p->peek             = s.peek;
    /* Trim any diagnostics that were emitted speculatively. The
     * `count` is the visible record count; over-cap emits were
     * counted in error_count without recording, so resetting
     * both is sufficient. Owned-message memory remains in the
     * collector's allocator until dispose — a minor leak we
     * accept for speculative parsing simplicity. */
    p->diag->count        = s.diag_count;
    p->diag->error_count  = s.diag_error_count;
    p->diag->truncated    = s.diag_truncated;
    p->oom                = s.oom;
}

static bool is_type_starter(zsp_token_kind_t k)
{
    switch (k) {
    case ZSP_TOK_IDENT:
    case ZSP_TOK_KW_BOOL:
    case ZSP_TOK_KW_STRING:
    case ZSP_TOK_KW_BYTES:
    case ZSP_TOK_KW_EXTERN:
    case ZSP_TOK_KW_VARINT:
    case ZSP_TOK_KW_VARUINT:
    case ZSP_TOK_KW_VARSIZE:
    case ZSP_TOK_KW_BIT:
    case ZSP_TOK_KW_INT:
    case ZSP_TOK_KW_PACKED:
    case ZSP_TOK_KW_OPTIONAL:
    case ZSP_TOK_KW_IMPLICIT:
    case ZSP_TOK_KW_EXTEND:
    case ZSP_TOK_KW_DEPRECATED:
    case ZSP_TOK_KW_REMOVED:
    case ZSP_TOK_KW_INDEX:
        return true;
    default:
        return false;
    }
}

static bool expect(parser_t *p, zsp_token_kind_t k, const char *msg)
{
    if (eat(p, k)) {
        return true;
    }
    emit_error_at_cur(p, msg);
    return false;
}

/* Panic-mode skip: advance until cur kind is in the sync set or
 * EOF is reached. Sync kinds are passed via a small fixed-size
 * array. */
static void skip_to_any(parser_t *p,
                        const zsp_token_kind_t *sync, size_t n)
{
    while (!check(p, ZSP_TOK_EOF)) {
        for (size_t i = 0U; i < n; ++i) {
            if (p->cur.kind == sync[i]) {
                return;
            }
        }
        advance(p);
    }
}

/* ============================================================ */
/* AST construction helpers                                      */
/* ============================================================ */

static zsp_ast_node_t *new_node(parser_t *p, zsp_ast_kind_t kind,
                                uint32_t start_offset)
{
    const uint32_t end = p->cur.offset;
    const uint32_t len = (end >= start_offset) ? (end - start_offset) : 0U;
    zsp_ast_node_t *const n = zsp_ast_new(p->arena, kind, p->file_id,
                                          start_offset, len);
    if (n == NULL) {
        p->oom = true;
    }
    return n;
}

static zsp_ast_node_t *new_node_from_tok(parser_t *p, zsp_ast_kind_t kind,
                                         const zsp_token_t *tok)
{
    zsp_ast_node_t *const n = zsp_ast_new(p->arena, kind, p->file_id,
                                          tok->offset, tok->length);
    if (n == NULL) {
        p->oom = true;
    }
    return n;
}

/* Push a node onto a list with OOM tracking. Returns false if
 * OOM, true otherwise (whether or not the node was non-NULL). */
static bool push_or_oom(parser_t *p, zsp_ast_list_t *l, zsp_ast_node_t *n)
{
    if (n == NULL) {
        return !p->oom;
    }
    if (zsp_ast_list_push(l, n) != ZSERIO_OK) {
        p->oom = true;
        return false;
    }
    return true;
}

/* ============================================================ */
/* Forward declarations                                          */
/* ============================================================ */

static zsp_ast_node_t *parse_ident(parser_t *p);
static zsp_ast_node_t *parse_dotted_name(parser_t *p);
static zsp_ast_node_t *parse_type(parser_t *p);
static zsp_ast_node_t *parse_expression(parser_t *p);
static zsp_ast_node_t *parse_ternary(parser_t *p);
static zsp_ast_node_t *parse_primary(parser_t *p);
static zsp_ast_node_t *parse_binop_rhs(parser_t *p, int min_prec,
                                       zsp_ast_node_t *lhs);
static zsp_ast_node_t *parse_decl(parser_t *p, zsp_ast_node_t **opt_anno);
static zsp_ast_node_t *parse_struct_or_union(parser_t *p,
                                             zsp_ast_node_t **template_params,
                                             size_t n_template_params,
                                             bool is_union);
static zsp_ast_node_t *parse_choice(parser_t *p,
                                    zsp_ast_node_t **template_params,
                                    size_t n_template_params);
static zsp_ast_node_t *parse_enum_decl(parser_t *p);
static zsp_ast_node_t *parse_bitmask(parser_t *p);
static zsp_ast_node_t *parse_subtype(parser_t *p,
                                     zsp_ast_node_t **template_params,
                                     size_t n_template_params);
static zsp_ast_node_t *parse_const(parser_t *p);
static zsp_ast_node_t *parse_instantiate(parser_t *p);
static zsp_ast_node_t *parse_rule_group(parser_t *p);
static zsp_ast_node_t *parse_service(parser_t *p);
static zsp_ast_node_t *parse_pubsub(parser_t *p);
static zsp_ast_node_t *parse_sql_table(parser_t *p);
static zsp_ast_node_t *parse_sql_database(parser_t *p);
static zsp_ast_node_t *parse_field(parser_t *p);
static zsp_ast_node_t *parse_choice_case_or_default(parser_t *p);
static bool            parse_template_params(parser_t *p,
                                             zsp_ast_node_t ***out_items,
                                             size_t *out_count);
static bool            parse_compound_params(parser_t *p,
                                             zsp_ast_node_t ***out_items,
                                             size_t *out_count);
static zsp_ast_node_t *parse_annotation(parser_t *p);
static void            try_split_rshift(parser_t *p);

/* ============================================================ */
/* Identifiers, dotted names                                     */
/* ============================================================ */

static zsp_ast_node_t *parse_ident(parser_t *p)
{
    if (!check(p, ZSP_TOK_IDENT)) {
        emit_error_at_cur(p, "expected identifier");
        return NULL;
    }
    zsp_ast_node_t *const n = new_node_from_tok(p, ZSP_AST_EXPR_IDENT, &p->cur);
    if (n != NULL) {
        n->u.ident.span_off = p->cur.offset;
        n->u.ident.span_len = p->cur.length;
    }
    advance(p);
    return n;
}

static zsp_ast_node_t *parse_dotted_name(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    zsp_ast_node_t *const first = parse_ident(p);
    if (first == NULL) {
        return NULL;
    }
    /* Only enter list-mode if the dot is followed by an
     * identifier — preserves `path . *` for wildcard imports
     * and `path . STAR` shouldn't be eaten here. */
    if (!check(p, ZSP_TOK_DOT) || (p->peek.kind != ZSP_TOK_IDENT)) {
        return first;
    }

    zsp_ast_list_t parts;
    zsp_ast_list_init(&parts, p->arena);
    if (!push_or_oom(p, &parts, first)) {
        return NULL;
    }
    while (check(p, ZSP_TOK_DOT) && (p->peek.kind == ZSP_TOK_IDENT)) {
        advance(p);   /* eat the dot */
        zsp_ast_node_t *const next = parse_ident(p);
        if (next == NULL) {
            return NULL;
        }
        if (!push_or_oom(p, &parts, next)) {
            return NULL;
        }
    }
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_DOTTED_NAME, start);
    if (n == NULL) {
        return NULL;
    }
    zsp_ast_list_take(&parts, &n->u.dotted.parts, &n->u.dotted.n_parts);
    return n;
}

/* ============================================================ */
/* Builtin-type lookup table                                     */
/* ============================================================ */

typedef struct {
    zsp_token_kind_t   tk;
    zsp_builtin_code_t code;
} builtin_kw_t;

static const builtin_kw_t BUILTIN_KW_DIRECT[] = {
    { ZSP_TOK_KW_BOOL,     ZSP_TB_BOOL },
    { ZSP_TOK_KW_STRING,   ZSP_TB_STRING },
    { ZSP_TOK_KW_BYTES,    ZSP_TB_BYTES },
    { ZSP_TOK_KW_EXTERN,   ZSP_TB_EXTERN },
    { ZSP_TOK_KW_VARINT,   ZSP_TB_VARINT },
    { ZSP_TOK_KW_VARUINT,  ZSP_TB_VARUINT },
    { ZSP_TOK_KW_VARSIZE,  ZSP_TB_VARSIZE },
};

/* Identifiers like "int8", "uint32", "float32" are NOT keywords
 * in upstream's grammar but ARE recognised as builtin types in
 * the analyser. We promote them at parse time when they appear
 * as a type, by comparing the source text. */
typedef struct {
    const char        *name;
    size_t             len;
    zsp_builtin_code_t code;
} builtin_ident_t;

#define BI(text, code_) { (text), sizeof(text) - 1U, (code_) }

static const builtin_ident_t BUILTIN_IDENTS[] = {
    BI("int8",      ZSP_TB_INT8),
    BI("int16",     ZSP_TB_INT16),
    BI("int32",     ZSP_TB_INT32),
    BI("int64",     ZSP_TB_INT64),
    BI("uint8",     ZSP_TB_UINT8),
    BI("uint16",    ZSP_TB_UINT16),
    BI("uint32",    ZSP_TB_UINT32),
    BI("uint64",    ZSP_TB_UINT64),
    BI("varint16",  ZSP_TB_VARINT16),
    BI("varint32",  ZSP_TB_VARINT32),
    BI("varint64",  ZSP_TB_VARINT64),
    BI("varuint16", ZSP_TB_VARUINT16),
    BI("varuint32", ZSP_TB_VARUINT32),
    BI("varuint64", ZSP_TB_VARUINT64),
    BI("float16",   ZSP_TB_FLOAT16),
    BI("float32",   ZSP_TB_FLOAT32),
    BI("float64",   ZSP_TB_FLOAT64),
};

#undef BI
#define BUILTIN_IDENT_COUNT (sizeof(BUILTIN_IDENTS) / sizeof(BUILTIN_IDENTS[0]))

static zsp_builtin_code_t identify_builtin_ident(const uint8_t *bytes, size_t len)
{
    for (size_t i = 0U; i < BUILTIN_IDENT_COUNT; ++i) {
        if ((BUILTIN_IDENTS[i].len == len)
                && (memcmp(BUILTIN_IDENTS[i].name, bytes, len) == 0)) {
            return BUILTIN_IDENTS[i].code;
        }
    }
    return ZSP_TB_UNKNOWN;
}

static const uint8_t *file_bytes(const parser_t *p)
{
    const zsp_file_t *f = NULL;
    if (!zsp_source_get((const zsp_source_t *)p->lexer.source,
                        p->file_id, &f)) {
        return NULL;
    }
    return f->bytes;
}

/* ============================================================ */
/* Types                                                         */
/* ============================================================ */

static zsp_ast_node_t *parse_bitfield(parser_t *p, zsp_builtin_code_t code)
{
    /* current is 'bit' or 'int' — advance past it then accept one
     * of three width forms:
     *   bit:N            (fixed width literal)
     *   bit:(expr)       (parenthesised expression, fixed at compile time)
     *   bit<expr>        (angle-bracket form for dynamic-width bitfields)
     */
    const uint32_t start = p->cur.offset;
    advance(p);

    uint32_t width = 0U;
    zsp_ast_node_t *width_expr = NULL;
    if (eat(p, ZSP_TOK_COLON)) {
        if (check(p, ZSP_TOK_INT_LIT)) {
            width = (uint32_t)p->cur.value.u64;
            advance(p);
        }
        else if (eat(p, ZSP_TOK_LPAREN)) {
            width_expr = parse_expression(p);
            (void)expect(p, ZSP_TOK_RPAREN, "expected ')' to close bit width");
        }
        else {
            emit_error_at_cur(p, "expected bit-field width");
            return NULL;
        }
    }
    else if (eat(p, ZSP_TOK_LT)) {
        /* Parse the bit-width expression at a precedence level
         * that excludes relational operators (`<`, `>`, `<=`,
         * `>=` are precedence 7); otherwise the closing `>`
         * would be eaten as a comparison operator. Shift-left
         * and shift-right (precedence 8) still bind normally.
         * Handle a top-level ternary tail explicitly — the
         * normal `parse_ternary` would consume `>` inside its
         * cond at full precedence, which conflicts with the
         * closing delimiter. */
        const uint32_t expr_start = p->cur.offset;
        zsp_ast_node_t *cond = parse_primary(p);
        if (cond != NULL) {
            cond = parse_binop_rhs(p, 8, cond);
        }
        width_expr = cond;
        if ((cond != NULL) && eat(p, ZSP_TOK_QUESTION)) {
            /* Inside the THEN/ELSE branches, comparisons in
             * sub-expressions are still constrained — schemas
             * needing `>` must use parentheses. THEN is bounded
             * by the `:`. */
            zsp_ast_node_t *const then_e = parse_ternary(p);
            (void)expect(p, ZSP_TOK_COLON, "expected ':' in ternary expression");
            zsp_ast_node_t *else_e = parse_primary(p);
            if (else_e != NULL) {
                else_e = parse_binop_rhs(p, 8, else_e);
            }
            zsp_ast_node_t *const tn = new_node(p, ZSP_AST_EXPR_TERNARY, expr_start);
            if (tn != NULL) {
                tn->u.ternary.cond      = cond;
                tn->u.ternary.then_expr = then_e;
                tn->u.ternary.else_expr = else_e;
            }
            width_expr = tn;
        }
        try_split_rshift(p);
        if (!expect(p, ZSP_TOK_GT, "expected '>' to close bit width")) {
            return NULL;
        }
    }
    else {
        /* Standalone `bit` / `int` (no width) — dynamic-width
         * bitfield. Width 0 is the sentinel; the analyser /
         * runtime resolves the actual width from the surrounding
         * `int<expr>` style context elsewhere. */
        width = 0U;
    }
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_TYPE_BUILTIN, start);
    if (n != NULL) {
        n->u.type_builtin.code  = code;
        n->u.type_builtin.width = width;
        n->u.type_builtin.width_expr = width_expr;
    }
    return n;
}

static bool parse_type_args(parser_t *p,
                            zsp_ast_node_t ***out_items, size_t *out_count)
{
    *out_items = NULL;
    *out_count = 0U;
    if (!eat(p, ZSP_TOK_LPAREN)) {
        return true;
    }
    if (eat(p, ZSP_TOK_RPAREN)) {
        return true;
    }
    zsp_ast_list_t list;
    zsp_ast_list_init(&list, p->arena);
    for (;;) {
        /* Optional `explicit` modifier on a type-argument
         * binding: `Foo(explicit X)`. Consume and ignore for
         * the parser; the analyser uses the explicit-vs-implicit
         * distinction. */
        (void)eat(p, ZSP_TOK_KW_EXPLICIT);
        zsp_ast_node_t *const e = parse_expression(p);
        if (!push_or_oom(p, &list, e)) {
            return false;
        }
        if (!eat(p, ZSP_TOK_COMMA)) {
            break;
        }
    }
    if (!expect(p, ZSP_TOK_RPAREN, "expected ')' after type arguments")) {
        return false;
    }
    zsp_ast_list_take(&list, out_items, out_count);
    return true;
}

/* `>>` is lexed as a single ZSP_TOK_RSHIFT but the parser may
 * need to treat it as two `>` tokens to close nested templates
 * (`Foo<Bar<Baz>>`). When the closing-`>` site sees RSHIFT,
 * virtually split it: cur becomes GT (representing the first
 * `>`), the second `>` is pushed onto peek, and the original
 * peek is stashed for the subsequent advance. */
static void try_split_rshift(parser_t *p)
{
    if (p->cur.kind != ZSP_TOK_RSHIFT) {
        return;
    }
    /* Preserve the original peek for after both `>` tokens are
     * consumed. */
    if (!p->has_stash) {
        p->stash     = p->peek;
        p->has_stash = true;
    }
    /* Replace peek with the synthesized second `>` at offset+1. */
    p->peek = p->cur;
    p->peek.kind   = ZSP_TOK_GT;
    p->peek.offset = p->cur.offset + 1U;
    p->peek.length = 1U;
    /* Replace cur with the first `>`. */
    p->cur.kind   = ZSP_TOK_GT;
    p->cur.length = 1U;
}

static bool parse_template_args(parser_t *p,
                                zsp_ast_node_t ***out_items, size_t *out_count)
{
    *out_items = NULL;
    *out_count = 0U;
    if (!eat(p, ZSP_TOK_LT)) {
        return true;
    }
    zsp_ast_list_t list;
    zsp_ast_list_init(&list, p->arena);
    for (;;) {
        zsp_ast_node_t *const t = parse_type(p);
        if (!push_or_oom(p, &list, t)) {
            return false;
        }
        if (!eat(p, ZSP_TOK_COMMA)) {
            break;
        }
    }
    /* Accept `>` or the first half of a `>>`. */
    try_split_rshift(p);
    if (!expect(p, ZSP_TOK_GT, "expected '>' after template arguments")) {
        return false;
    }
    zsp_ast_list_take(&list, out_items, out_count);
    return true;
}

static zsp_ast_node_t *parse_type(parser_t *p)
{
    const uint32_t start = p->cur.offset;

    /* Direct keyword builtins. */
    for (size_t i = 0U; i < sizeof(BUILTIN_KW_DIRECT)/sizeof(BUILTIN_KW_DIRECT[0]); ++i) {
        if (check(p, BUILTIN_KW_DIRECT[i].tk)) {
            const zsp_builtin_code_t code = BUILTIN_KW_DIRECT[i].code;
            advance(p);
            zsp_ast_node_t *const n = new_node(p, ZSP_AST_TYPE_BUILTIN, start);
            if (n != NULL) {
                n->u.type_builtin.code  = code;
                n->u.type_builtin.width = 0U;
            }
            return n;
        }
    }
    /* bit:N and int:N */
    if (check(p, ZSP_TOK_KW_BIT)) {
        return parse_bitfield(p, ZSP_TB_BIT_FIELD);
    }
    if (check(p, ZSP_TOK_KW_INT)) {
        return parse_bitfield(p, ZSP_TB_INT_FIELD);
    }

    /* Identifier-shaped types — could be a builtin (int8 etc.)
     * or a user-defined named type. */
    if (!check(p, ZSP_TOK_IDENT)) {
        emit_error_at_cur(p, "expected type");
        return NULL;
    }

    const uint8_t *src = file_bytes(p);
    const zsp_builtin_code_t bcode = (src != NULL)
            ? identify_builtin_ident(&src[p->cur.offset], p->cur.length)
            : ZSP_TB_UNKNOWN;

    if (bcode != ZSP_TB_UNKNOWN) {
        advance(p);
        zsp_ast_node_t *const n = new_node(p, ZSP_AST_TYPE_BUILTIN, start);
        if (n != NULL) {
            n->u.type_builtin.code  = bcode;
            n->u.type_builtin.width = 0U;
        }
        return n;
    }

    /* Named type: dotted_name template_args? type_args? */
    zsp_ast_node_t *const name = parse_dotted_name(p);
    if (name == NULL) {
        return NULL;
    }
    zsp_ast_node_t **tparams = NULL;
    size_t           n_tparams = 0U;
    if (!parse_template_args(p, &tparams, &n_tparams)) {
        return NULL;
    }
    zsp_ast_node_t **targs = NULL;
    size_t           n_targs = 0U;
    if (!parse_type_args(p, &targs, &n_targs)) {
        return NULL;
    }
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_TYPE_REF, start);
    if (n != NULL) {
        n->u.type_ref.name             = name;
        n->u.type_ref.template_args    = tparams;
        n->u.type_ref.n_template_args  = n_tparams;
        n->u.type_ref.type_args        = targs;
        n->u.type_ref.n_type_args      = n_targs;
    }
    return n;
}

/* ============================================================ */
/* Expressions (Pratt)                                           */
/* ============================================================ */

static int binop_precedence(zsp_token_kind_t k)
{
    switch (k) {
    case ZSP_TOK_LOGICAL_OR:  return 1;
    case ZSP_TOK_LOGICAL_AND: return 2;
    case ZSP_TOK_PIPE:        return 3;
    case ZSP_TOK_CARET:       return 4;
    case ZSP_TOK_AMP:         return 5;
    case ZSP_TOK_EQ:
    case ZSP_TOK_NEQ:         return 6;
    case ZSP_TOK_LT:
    case ZSP_TOK_LE:
    case ZSP_TOK_GT:
    case ZSP_TOK_GE:          return 7;
    case ZSP_TOK_LSHIFT:
    case ZSP_TOK_RSHIFT:      return 8;
    case ZSP_TOK_PLUS:
    case ZSP_TOK_MINUS:       return 9;
    case ZSP_TOK_STAR:
    case ZSP_TOK_SLASH:
    case ZSP_TOK_PERCENT:     return 10;
    default:                  return 0;
    }
}

static zsp_op_code_t binop_code(zsp_token_kind_t k)
{
    switch (k) {
    case ZSP_TOK_LOGICAL_OR:  return ZSP_OP_LOGICAL_OR;
    case ZSP_TOK_LOGICAL_AND: return ZSP_OP_LOGICAL_AND;
    case ZSP_TOK_PIPE:        return ZSP_OP_BIT_OR;
    case ZSP_TOK_CARET:       return ZSP_OP_BIT_XOR;
    case ZSP_TOK_AMP:         return ZSP_OP_BIT_AND;
    case ZSP_TOK_EQ:          return ZSP_OP_EQ;
    case ZSP_TOK_NEQ:         return ZSP_OP_NEQ;
    case ZSP_TOK_LT:          return ZSP_OP_LT;
    case ZSP_TOK_LE:          return ZSP_OP_LE;
    case ZSP_TOK_GT:          return ZSP_OP_GT;
    case ZSP_TOK_GE:          return ZSP_OP_GE;
    case ZSP_TOK_LSHIFT:      return ZSP_OP_LSHIFT;
    case ZSP_TOK_RSHIFT:      return ZSP_OP_RSHIFT;
    case ZSP_TOK_PLUS:        return ZSP_OP_ADD;
    case ZSP_TOK_MINUS:       return ZSP_OP_SUB;
    case ZSP_TOK_STAR:        return ZSP_OP_MUL;
    case ZSP_TOK_SLASH:       return ZSP_OP_DIV;
    case ZSP_TOK_PERCENT:     return ZSP_OP_MOD;
    default:                  return ZSP_OP_UNKNOWN;
    }
}

static zsp_ast_node_t *parse_primary(parser_t *p)
{
    const uint32_t start = p->cur.offset;

    if (check(p, ZSP_TOK_INT_LIT)) {
        zsp_ast_node_t *const n = new_node_from_tok(p, ZSP_AST_EXPR_INT_LIT, &p->cur);
        if (n != NULL) {
            n->u.int_lit.value = p->cur.value.u64;
        }
        advance(p);
        return n;
    }
    if (check(p, ZSP_TOK_FLOAT_LIT)) {
        zsp_ast_node_t *const n = new_node_from_tok(p, ZSP_AST_EXPR_FLOAT_LIT, &p->cur);
        if (n != NULL) {
            n->u.float_lit.value = p->cur.value.f64;
        }
        advance(p);
        return n;
    }
    if (check(p, ZSP_TOK_STRING_LIT)) {
        zsp_ast_node_t *const n = new_node_from_tok(p, ZSP_AST_EXPR_STRING_LIT, &p->cur);
        if (n != NULL) {
            n->u.string_lit.span_off = p->cur.offset;
            n->u.string_lit.span_len = p->cur.length;
        }
        advance(p);
        return n;
    }
    if (check(p, ZSP_TOK_KW_TRUE) || check(p, ZSP_TOK_KW_FALSE)) {
        zsp_ast_node_t *const n = new_node_from_tok(p, ZSP_AST_EXPR_BOOL_LIT, &p->cur);
        if (n != NULL) {
            n->u.bool_lit.value = check(p, ZSP_TOK_KW_TRUE);
        }
        advance(p);
        return n;
    }
    if (eat(p, ZSP_TOK_LPAREN)) {
        zsp_ast_node_t *const e = parse_expression(p);
        if (!expect(p, ZSP_TOK_RPAREN, "expected ')' after expression")) {
            return NULL;
        }
        return e;
    }
    /* Unary prefix */
    if (check(p, ZSP_TOK_MINUS) || check(p, ZSP_TOK_BANG)
            || check(p, ZSP_TOK_TILDE) || check(p, ZSP_TOK_PLUS)) {
        const zsp_token_kind_t op_tok = p->cur.kind;
        advance(p);
        zsp_ast_node_t *const operand = parse_primary(p);
        if (operand == NULL) {
            return NULL;
        }
        zsp_ast_node_t *const n = new_node(p, ZSP_AST_EXPR_UNARY, start);
        if (n != NULL) {
            switch (op_tok) {
            case ZSP_TOK_MINUS: n->u.unop.op = ZSP_OP_NEG;         break;
            case ZSP_TOK_BANG:  n->u.unop.op = ZSP_OP_LOGICAL_NOT; break;
            case ZSP_TOK_TILDE: n->u.unop.op = ZSP_OP_BIT_NOT;     break;
            default:            n->u.unop.op = ZSP_OP_UNKNOWN;     break;
            }
            n->u.unop.operand = operand;
        }
        return n;
    }
    /* Function-call-like prefixes: lengthof/valueof/numbits/isset.
     * Emit as EXPR_CALL with the keyword as a synthetic IDENT
     * callee so the emitter's builtin-call path (which already
     * lowers each name into its expansion) and the resolver
     * (which inlines schema functions through EXPR_CALL) treat
     * them uniformly. */
    if (check(p, ZSP_TOK_KW_LENGTHOF) || check(p, ZSP_TOK_KW_VALUEOF)
            || check(p, ZSP_TOK_KW_NUMBITS) || check(p, ZSP_TOK_KW_ISSET)) {
        const zsp_token_t kw_tok = p->cur;
        advance(p);
        if (!expect(p, ZSP_TOK_LPAREN, "expected '(' after builtin call")) {
            return NULL;
        }
        zsp_ast_node_t *args[4];
        size_t n_args = 0U;
        args[n_args++] = parse_expression(p);
        while (eat(p, ZSP_TOK_COMMA) && (n_args < (sizeof(args)/sizeof(args[0])))) {
            args[n_args++] = parse_expression(p);
        }
        if (!expect(p, ZSP_TOK_RPAREN, "expected ')' to close builtin call")) {
            return NULL;
        }
        zsp_ast_node_t *const callee = new_node_from_tok(p, ZSP_AST_EXPR_IDENT, &kw_tok);
        if (callee != NULL) {
            callee->u.ident.span_off = kw_tok.offset;
            callee->u.ident.span_len = kw_tok.length;
        }
        zsp_ast_node_t *const n = new_node(p, ZSP_AST_EXPR_CALL, start);
        if (n != NULL) {
            n->u.call.callee = callee;
            n->u.call.n_args = n_args;
            if (n_args > 0U) {
                n->u.call.args = (zsp_ast_node_t **)zsp_arena_alloc(
                        p->arena, n_args * sizeof(zsp_ast_node_t *),
                        sizeof(void *));
                if (n->u.call.args != NULL) {
                    (void)memcpy(n->u.call.args, args, n_args * sizeof(zsp_ast_node_t *));
                }
            }
        }
        return n;
    }
    /* @index pseudo-identifier: appears in array-element type
     * parameter contexts (e.g. `Block(headers[@index])`). The
     * keyword is lexed as ZSP_TOK_KW_INDEX; in an expression
     * context it acts as an identifier that the analyser
     * resolves to the surrounding loop's iteration index. */
    if (check(p, ZSP_TOK_KW_INDEX)) {
        zsp_ast_node_t *const n = new_node_from_tok(p, ZSP_AST_EXPR_IDENT, &p->cur);
        if (n != NULL) {
            n->u.ident.span_off = p->cur.offset;
            n->u.ident.span_len = p->cur.length;
        }
        advance(p);
        return n;
    }
    /* Identifier (potentially compounded with `.`, `[]`, `()`). */
    if (check(p, ZSP_TOK_IDENT)) {
        zsp_ast_node_t *const id = parse_ident(p);
        if (id == NULL) {
            return NULL;
        }
        zsp_ast_node_t *cur_expr = id;
        /* Postfix chain: .name | [expr] | (args) */
        for (;;) {
            if (eat(p, ZSP_TOK_DOT)) {
                zsp_ast_node_t *const name = parse_ident(p);
                if (name == NULL) {
                    return NULL;
                }
                zsp_ast_node_t *const n = new_node(p, ZSP_AST_EXPR_MEMBER, start);
                if (n == NULL) {
                    return NULL;
                }
                n->u.member.lhs  = cur_expr;
                n->u.member.name = name;
                cur_expr = n;
            }
            else if (eat(p, ZSP_TOK_LBRACKET)) {
                zsp_ast_node_t *const ix = parse_expression(p);
                if (!expect(p, ZSP_TOK_RBRACKET, "expected ']' after index")) {
                    return NULL;
                }
                zsp_ast_node_t *const n = new_node(p, ZSP_AST_EXPR_INDEX, start);
                if (n == NULL) {
                    return NULL;
                }
                n->u.index.lhs   = cur_expr;
                n->u.index.index = ix;
                cur_expr = n;
            }
            else if (eat(p, ZSP_TOK_LPAREN)) {
                zsp_ast_list_t args;
                zsp_ast_list_init(&args, p->arena);
                if (!check(p, ZSP_TOK_RPAREN)) {
                    for (;;) {
                        zsp_ast_node_t *const a = parse_expression(p);
                        if (!push_or_oom(p, &args, a)) {
                            return NULL;
                        }
                        if (!eat(p, ZSP_TOK_COMMA)) {
                            break;
                        }
                    }
                }
                if (!expect(p, ZSP_TOK_RPAREN, "expected ')' after arguments")) {
                    return NULL;
                }
                zsp_ast_node_t *const n = new_node(p, ZSP_AST_EXPR_CALL, start);
                if (n == NULL) {
                    return NULL;
                }
                n->u.call.callee = cur_expr;
                zsp_ast_list_take(&args, &n->u.call.args, &n->u.call.n_args);
                cur_expr = n;
            }
            else {
                break;
            }
        }
        return cur_expr;
    }

    emit_error_at_cur(p, "expected expression");
    return NULL;
}

static zsp_ast_node_t *parse_binop_rhs(parser_t *p, int min_prec,
                                       zsp_ast_node_t *lhs)
{
    for (;;) {
        const int prec = binop_precedence(p->cur.kind);
        if (prec < min_prec) {
            return lhs;
        }
        const zsp_op_code_t op = binop_code(p->cur.kind);
        const uint32_t      start = lhs->offset;
        advance(p);
        zsp_ast_node_t *rhs = parse_primary(p);
        if (rhs == NULL) {
            return NULL;
        }
        const int next_prec = binop_precedence(p->cur.kind);
        if (next_prec > prec) {
            rhs = parse_binop_rhs(p, prec + 1, rhs);
            if (rhs == NULL) {
                return NULL;
            }
        }
        zsp_ast_node_t *const n = new_node(p, ZSP_AST_EXPR_BINARY, start);
        if (n == NULL) {
            return NULL;
        }
        n->u.binop.op  = op;
        n->u.binop.lhs = lhs;
        n->u.binop.rhs = rhs;
        lhs = n;
    }
}

static zsp_ast_node_t *parse_ternary(parser_t *p)
{
    zsp_ast_node_t *cond = parse_primary(p);
    if (cond == NULL) {
        return NULL;
    }
    cond = parse_binop_rhs(p, 1, cond);
    if (cond == NULL) {
        return NULL;
    }
    if (eat(p, ZSP_TOK_QUESTION)) {
        const uint32_t start = cond->offset;
        zsp_ast_node_t *const then_e = parse_ternary(p);
        if (!expect(p, ZSP_TOK_COLON, "expected ':' in ternary expression")) {
            return NULL;
        }
        zsp_ast_node_t *const else_e = parse_ternary(p);
        zsp_ast_node_t *const n = new_node(p, ZSP_AST_EXPR_TERNARY, start);
        if (n != NULL) {
            n->u.ternary.cond      = cond;
            n->u.ternary.then_expr = then_e;
            n->u.ternary.else_expr = else_e;
        }
        return n;
    }
    return cond;
}

static zsp_ast_node_t *parse_expression(parser_t *p)
{
    return parse_ternary(p);
}

/* ============================================================ */
/* Annotations                                                   */
/* ============================================================ */

static zsp_ast_node_t *parse_annotation(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    zsp_annotation_code_t code = ZSP_ANNOT_DEPRECATED;
    if (check(p, ZSP_TOK_KW_DEPRECATED))      { code = ZSP_ANNOT_DEPRECATED; }
    else if (check(p, ZSP_TOK_KW_REMOVED))    { code = ZSP_ANNOT_REMOVED; }
    else if (check(p, ZSP_TOK_KW_INDEX))      { code = ZSP_ANNOT_INDEX; }
    else {
        emit_error_at_cur(p, "expected annotation");
        return NULL;
    }
    advance(p);
    zsp_ast_node_t *message = NULL;
    if (eat(p, ZSP_TOK_LPAREN)) {
        message = parse_expression(p);
        (void)expect(p, ZSP_TOK_RPAREN, "expected ')' after annotation message");
    }
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_ANNOTATION, start);
    if (n != NULL) {
        n->u.annotation.code    = code;
        n->u.annotation.message = message;
    }
    return n;
}

/* ============================================================ */
/* Compound declarations: struct, union, choice                  */
/* ============================================================ */

static bool parse_template_params(parser_t *p,
                                  zsp_ast_node_t ***out_items, size_t *out_count)
{
    *out_items = NULL;
    *out_count = 0U;
    if (!eat(p, ZSP_TOK_LT)) {
        return true;
    }
    zsp_ast_list_t list;
    zsp_ast_list_init(&list, p->arena);
    for (;;) {
        const uint32_t start = p->cur.offset;
        zsp_ast_node_t *const name = parse_ident(p);
        if (name == NULL) {
            return false;
        }
        zsp_ast_node_t *const n = new_node(p, ZSP_AST_TEMPLATE_PARAM, start);
        if (n == NULL) {
            return false;
        }
        n->u.template_param.name = name;
        if (!push_or_oom(p, &list, n)) {
            return false;
        }
        if (!eat(p, ZSP_TOK_COMMA)) {
            break;
        }
    }
    try_split_rshift(p);
    if (!expect(p, ZSP_TOK_GT, "expected '>' after template parameters")) {
        return false;
    }
    zsp_ast_list_take(&list, out_items, out_count);
    return true;
}

static bool parse_compound_params(parser_t *p,
                                  zsp_ast_node_t ***out_items, size_t *out_count)
{
    *out_items = NULL;
    *out_count = 0U;
    if (!eat(p, ZSP_TOK_LPAREN)) {
        return true;
    }
    if (eat(p, ZSP_TOK_RPAREN)) {
        return true;
    }
    zsp_ast_list_t list;
    zsp_ast_list_init(&list, p->arena);
    for (;;) {
        const uint32_t start = p->cur.offset;
        zsp_ast_node_t *const ty = parse_type(p);
        if (ty == NULL) {
            return false;
        }
        zsp_ast_node_t *const name = parse_ident(p);
        if (name == NULL) {
            return false;
        }
        zsp_ast_node_t *const n = new_node(p, ZSP_AST_PARAMETER, start);
        if (n != NULL) {
            n->u.parameter.type = ty;
            n->u.parameter.name = name;
        }
        if (!push_or_oom(p, &list, n)) {
            return false;
        }
        if (!eat(p, ZSP_TOK_COMMA)) {
            break;
        }
    }
    if (!expect(p, ZSP_TOK_RPAREN, "expected ')' after compound parameters")) {
        return false;
    }
    zsp_ast_list_take(&list, out_items, out_count);
    return true;
}

/* Field decl:
 *   annotations? align? offset? 'optional'? 'implicit'? type IDENT
 *      array-suffix? ( '=' expr )? if_clause? constraint? ';'
 *
 * Where array-suffix is the standard '[' expr? ']' attached to the
 * type (not the name). Constraints use the upstream ':' syntax.
 */
static zsp_ast_node_t *parse_field(parser_t *p)
{
    const uint32_t start = p->cur.offset;

    /* annotations */
    zsp_ast_list_t annos;
    zsp_ast_list_init(&annos, p->arena);
    while (check(p, ZSP_TOK_KW_DEPRECATED) || check(p, ZSP_TOK_KW_REMOVED)
            || check(p, ZSP_TOK_KW_INDEX)) {
        zsp_ast_node_t *const a = parse_annotation(p);
        if (!push_or_oom(p, &annos, a)) {
            return NULL;
        }
    }

    /* align(N): — upstream grammar trails the alignment
     * declaration with a `:`. */
    zsp_ast_node_t *align_expr = NULL;
    if (eat(p, ZSP_TOK_KW_ALIGN)) {
        if (expect(p, ZSP_TOK_LPAREN, "expected '(' after 'align'")) {
            align_expr = parse_expression(p);
            (void)expect(p, ZSP_TOK_RPAREN, "expected ')' after align value");
        }
        (void)eat(p, ZSP_TOK_COLON);
    }

    /* Leading field offset: an expression followed by ':' before
     * the type, e.g. `header.offset: int64 items[...];`. Detected
     * speculatively because there is no syntactic prefix that
     * distinguishes it from the type that would normally appear
     * here. We snapshot, try to parse an expression up to ':',
     * verify a type-starter follows, and either commit or rewind. */
    zsp_ast_node_t *offset_expr = NULL;
    if (check(p, ZSP_TOK_IDENT) || check(p, ZSP_TOK_KW_INDEX)) {
        const parser_snapshot_t snap = save_state(p);
        zsp_ast_node_t *const maybe = parse_expression(p);
        if ((maybe != NULL) && check(p, ZSP_TOK_COLON)
                && is_type_starter(p->peek.kind)) {
            advance(p);   /* eat the ':' */
            offset_expr = maybe;
        }
        else {
            restore_state(p, snap);
        }
    }

    bool optional_flag = false;
    bool implicit_flag = false;
    bool extend_flag   = false;

    /* `extend` is a field-level modifier introduced for forward-
     * compatible default-valued extensions. Appears as a leading
     * keyword. */
    if (eat(p, ZSP_TOK_KW_EXTEND)) {
        extend_flag = true;
    }
    if (eat(p, ZSP_TOK_KW_OPTIONAL)) {
        optional_flag = true;
    }
    if (eat(p, ZSP_TOK_KW_IMPLICIT)) {
        implicit_flag = true;
    }
    /* `extend` can also appear after optional/implicit per some
     * legacy schemas. */
    if (!extend_flag && eat(p, ZSP_TOK_KW_EXTEND)) {
        extend_flag = true;
    }
    /* extend_flag is stored on the AST below. */

    /* Type. May be `packed` prefix for packed arrays. */
    bool packed_flag = false;
    if (eat(p, ZSP_TOK_KW_PACKED)) {
        packed_flag = true;
    }

    zsp_ast_node_t *ty = parse_type(p);
    if (ty == NULL) {
        return NULL;
    }

    /* Identifier (field name). */
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }

    /* Array suffix attached to the field's type. */
    if (check(p, ZSP_TOK_LBRACKET)) {
        const uint32_t arr_start = ty->offset;
        advance(p);
        zsp_ast_node_t *length_expr = NULL;
        if (!check(p, ZSP_TOK_RBRACKET)) {
            length_expr = parse_expression(p);
        }
        if (!expect(p, ZSP_TOK_RBRACKET, "expected ']' to close array")) {
            return NULL;
        }
        zsp_ast_node_t *const arr = new_node(p, ZSP_AST_TYPE_ARRAY, arr_start);
        if (arr == NULL) {
            return NULL;
        }
        arr->u.type_array.element     = ty;
        arr->u.type_array.length_expr = length_expr;
        arr->u.type_array.packed      = packed_flag;
        arr->u.type_array.implicit    = implicit_flag;
        ty = arr;
        implicit_flag = false;
        packed_flag   = false;
    }

    /* '=' init */
    zsp_ast_node_t *init_expr = NULL;
    if (eat(p, ZSP_TOK_ASSIGN)) {
        init_expr = parse_expression(p);
    }

    /* if-clause */
    zsp_ast_node_t *if_clause = NULL;
    if (eat(p, ZSP_TOK_KW_IF)) {
        if_clause = parse_expression(p);
    }

    /* ':' constraint */
    zsp_ast_node_t *constraint = NULL;
    if (eat(p, ZSP_TOK_COLON)) {
        constraint = parse_expression(p);
    }

    if (!expect(p, ZSP_TOK_SEMICOLON, "expected ';' after field declaration")) {
        /* Recover at the next ';' or '}' */
        static const zsp_token_kind_t sync[] = {
            ZSP_TOK_SEMICOLON, ZSP_TOK_RBRACE
        };
        skip_to_any(p, sync, sizeof(sync) / sizeof(sync[0]));
        (void)eat(p, ZSP_TOK_SEMICOLON);
    }

    zsp_ast_node_t *const f = new_node(p, ZSP_AST_FIELD, start);
    if (f != NULL) {
        zsp_ast_list_take(&annos, &f->u.field.annotations, &f->u.field.n_annotations);
        f->u.field.align_expr      = align_expr;
        f->u.field.offset_expr     = offset_expr;
        f->u.field.type            = ty;
        f->u.field.name            = name;
        f->u.field.init_expr       = init_expr;
        f->u.field.if_clause       = if_clause;
        f->u.field.constraint_expr = constraint;
        f->u.field.optional        = optional_flag;
        f->u.field.implicit        = implicit_flag;
        f->u.field.extend          = extend_flag;
        f->u.field.autoid          = false;
    }
    return f;
}

static zsp_ast_node_t *parse_function(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);       /* consume 'function' */
    zsp_ast_node_t *const return_type = parse_type(p);
    if (return_type == NULL) {
        return NULL;
    }
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    (void)expect(p, ZSP_TOK_LPAREN, "expected '(' after function name");
    (void)expect(p, ZSP_TOK_RPAREN, "expected ')' (functions take no parameters)");
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' to start function body");
    (void)expect(p, ZSP_TOK_KW_RETURN, "expected 'return' in function body");
    zsp_ast_node_t *const body = parse_expression(p);
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after function body");
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to end function body");
    (void)eat(p, ZSP_TOK_SEMICOLON);
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_FUNCTION, start);
    if (n != NULL) {
        n->u.function_decl.return_type = return_type;
        n->u.function_decl.name        = name;
        n->u.function_decl.body_expr   = body;
    }
    return n;
}

static zsp_ast_node_t *parse_struct_or_union(parser_t *p,
                                             zsp_ast_node_t **tparams,
                                             size_t n_tparams,
                                             bool is_union)
{
    const uint32_t start = p->cur.offset;
    advance(p);    /* consume 'struct' or 'union' */
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    /* Template args after the name: `struct Name<T1, T2>(...)`. */
    if (check(p, ZSP_TOK_LT)) {
        if (!parse_template_params(p, &tparams, &n_tparams)) {
            return NULL;
        }
    }
    zsp_ast_node_t **params = NULL;
    size_t           n_params = 0U;
    if (!parse_compound_params(p, &params, &n_params)) {
        return NULL;
    }
    if (!expect(p, ZSP_TOK_LBRACE, "expected '{' to open compound body")) {
        return NULL;
    }
    zsp_ast_list_t members;
    zsp_ast_list_init(&members, p->arena);
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t before = p->cur.offset;
        zsp_ast_node_t *m;
        if (check(p, ZSP_TOK_KW_FUNCTION)) {
            m = parse_function(p);
        }
        else {
            m = parse_field(p);
        }
        if (m == NULL) {
            if (p->cur.offset == before) {
                advance(p);   /* force progress on stalled error */
            }
            continue;
        }
        if (!push_or_oom(p, &members, m)) {
            return NULL;
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close compound body");
    (void)eat(p, ZSP_TOK_SEMICOLON);
    zsp_ast_node_t *const n = new_node(p,
            is_union ? ZSP_AST_UNION : ZSP_AST_STRUCT, start);
    if (n != NULL) {
        n->u.compound.template_params   = tparams;
        n->u.compound.n_template_params = n_tparams;
        n->u.compound.name              = name;
        n->u.compound.params            = params;
        n->u.compound.n_params          = n_params;
        zsp_ast_list_take(&members, &n->u.compound.members,
                          &n->u.compound.n_members);
    }
    return n;
}

static zsp_ast_node_t *parse_choice_case_or_default(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    if (eat(p, ZSP_TOK_KW_DEFAULT)) {
        /* Two shapes: `default;` (no body) or `default: [field];`. */
        if (eat(p, ZSP_TOK_SEMICOLON)) {
            zsp_ast_node_t *const n = new_node(p, ZSP_AST_CHOICE_DEFAULT, start);
            if (n != NULL) {
                n->u.choice_default.field = NULL;
            }
            return n;
        }
        if (!expect(p, ZSP_TOK_COLON, "expected ':' after 'default'")) {
            return NULL;
        }
        zsp_ast_node_t *field = NULL;
        if (!check(p, ZSP_TOK_SEMICOLON) && !check(p, ZSP_TOK_RBRACE)) {
            field = parse_field(p);
        }
        else {
            (void)eat(p, ZSP_TOK_SEMICOLON);
        }
        zsp_ast_node_t *const n = new_node(p, ZSP_AST_CHOICE_DEFAULT, start);
        if (n != NULL) {
            n->u.choice_default.field = field;
        }
        return n;
    }
    /* `case EXPR (, EXPR)* ':' [field]` — allow multiple comma-
     * separated labels which fall through to one field. */
    if (!expect(p, ZSP_TOK_KW_CASE, "expected 'case' or 'default'")) {
        return NULL;
    }
    zsp_ast_list_t labels;
    zsp_ast_list_init(&labels, p->arena);
    for (;;) {
        zsp_ast_node_t *const e = parse_expression(p);
        if (!push_or_oom(p, &labels, e)) {
            return NULL;
        }
        if (!eat(p, ZSP_TOK_COMMA)) {
            break;
        }
        /* Upstream allows: `case A: case B: ...` (each `case` is its
         * own header). We handle the comma-list form (`case A, B:`)
         * by treating commas as continuations; the bare-case
         * iteration happens in the parent loop. */
    }
    if (!expect(p, ZSP_TOK_COLON, "expected ':' after case label(s)")) {
        return NULL;
    }
    zsp_ast_node_t *field = NULL;
    bool has_body = false;
    /* Empty case body: `case X: ; // no field` — accept a
     * lone semicolon as the field placeholder. The body is
     * "present but empty" (do NOT fall through to the next
     * case). */
    if (eat(p, ZSP_TOK_SEMICOLON)) {
        has_body = true;
    }
    else if (!check(p, ZSP_TOK_KW_CASE) && !check(p, ZSP_TOK_KW_DEFAULT)
            && !check(p, ZSP_TOK_RBRACE)) {
        field = parse_field(p);
        has_body = (field != NULL);
    }
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_CHOICE_CASE, start);
    if (n != NULL) {
        zsp_ast_list_take(&labels, &n->u.choice_case.labels, &n->u.choice_case.n_labels);
        n->u.choice_case.field    = field;
        n->u.choice_case.has_body = has_body;
    }
    return n;
}

static zsp_ast_node_t *parse_choice(parser_t *p,
                                    zsp_ast_node_t **tparams,
                                    size_t n_tparams)
{
    const uint32_t start = p->cur.offset;
    advance(p);     /* consume 'choice' */
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    if (check(p, ZSP_TOK_LT)) {
        if (!parse_template_params(p, &tparams, &n_tparams)) {
            return NULL;
        }
    }
    zsp_ast_node_t **params = NULL;
    size_t           n_params = 0U;
    if (!parse_compound_params(p, &params, &n_params)) {
        return NULL;
    }
    (void)expect(p, ZSP_TOK_KW_ON, "expected 'on' to introduce choice selector");
    /* Selector parens are optional in upstream grammar:
     *   `on (expr)` and `on expr` both valid. */
    const bool selector_parens = eat(p, ZSP_TOK_LPAREN);
    zsp_ast_node_t *const selector = parse_expression(p);
    if (selector_parens) {
        (void)expect(p, ZSP_TOK_RPAREN, "expected ')' after choice selector");
    }
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' to open choice body");

    zsp_ast_list_t cases;
    zsp_ast_list_init(&cases, p->arena);
    zsp_ast_node_t *default_case = NULL;
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t before = p->cur.offset;
        /* `function` declarations may appear inside a choice
         * body alongside cases. Parse + discard for the MVP
         * emitter; the analyser tracks them on the AST. */
        if (check(p, ZSP_TOK_KW_FUNCTION)) {
            (void)parse_function(p);
            if (p->cur.offset == before) {
                advance(p);
            }
            continue;
        }
        zsp_ast_node_t *const c = parse_choice_case_or_default(p);
        if (c == NULL) {
            if (p->cur.offset == before) {
                advance(p);
            }
            continue;
        }
        if (c->kind == ZSP_AST_CHOICE_DEFAULT) {
            default_case = c;
        }
        else {
            if (!push_or_oom(p, &cases, c)) {
                return NULL;
            }
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close choice body");
    (void)eat(p, ZSP_TOK_SEMICOLON);

    zsp_ast_node_t *const n = new_node(p, ZSP_AST_CHOICE, start);
    if (n != NULL) {
        n->u.choice.template_params   = tparams;
        n->u.choice.n_template_params = n_tparams;
        n->u.choice.name              = name;
        n->u.choice.params            = params;
        n->u.choice.n_params          = n_params;
        n->u.choice.selector          = selector;
        zsp_ast_list_take(&cases, &n->u.choice.cases, &n->u.choice.n_cases);
        n->u.choice.default_case      = default_case;
    }
    return n;
}

static zsp_ast_node_t *parse_enum_decl(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);    /* consume 'enum' */
    zsp_ast_node_t *const base = parse_type(p);
    if (base == NULL) {
        return NULL;
    }
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' to open enum body");
    zsp_ast_list_t items;
    zsp_ast_list_init(&items, p->arena);
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t istart = p->cur.offset;
        const uint32_t before = p->cur.offset;
        /* Optional leading annotations on enum items
         * (@deprecated, @removed, @index). Parse + drop for
         * now; analyser uses them on fields, not enum items. */
        while (check(p, ZSP_TOK_KW_DEPRECATED)
                || check(p, ZSP_TOK_KW_REMOVED)
                || check(p, ZSP_TOK_KW_INDEX)) {
            (void)parse_annotation(p);
        }
        zsp_ast_node_t *const iname = parse_ident(p);
        if ((iname == NULL) && (p->cur.offset == before)) {
            advance(p);
            continue;
        }
        zsp_ast_node_t *value = NULL;
        if (eat(p, ZSP_TOK_ASSIGN)) {
            value = parse_expression(p);
        }
        zsp_ast_node_t *const item = new_node(p, ZSP_AST_ENUM_ITEM, istart);
        if (item != NULL) {
            item->u.enum_item.name       = iname;
            item->u.enum_item.value_expr = value;
        }
        if (!push_or_oom(p, &items, item)) {
            return NULL;
        }
        if (!eat(p, ZSP_TOK_COMMA)) {
            break;
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close enum body");
    (void)eat(p, ZSP_TOK_SEMICOLON);
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_ENUM, start);
    if (n != NULL) {
        n->u.enum_decl.name      = name;
        n->u.enum_decl.base_type = base;
        zsp_ast_list_take(&items, &n->u.enum_decl.items, &n->u.enum_decl.n_items);
    }
    return n;
}

static zsp_ast_node_t *parse_bitmask(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);    /* consume 'bitmask' */
    zsp_ast_node_t *const base = parse_type(p);
    zsp_ast_node_t *const name = parse_ident(p);
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' to open bitmask body");
    zsp_ast_list_t items;
    zsp_ast_list_init(&items, p->arena);
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t istart = p->cur.offset;
        const uint32_t before = p->cur.offset;
        while (check(p, ZSP_TOK_KW_DEPRECATED)
                || check(p, ZSP_TOK_KW_REMOVED)
                || check(p, ZSP_TOK_KW_INDEX)) {
            (void)parse_annotation(p);
        }
        zsp_ast_node_t *const iname = parse_ident(p);
        if ((iname == NULL) && (p->cur.offset == before)) {
            advance(p);
            continue;
        }
        zsp_ast_node_t *value = NULL;
        if (eat(p, ZSP_TOK_ASSIGN)) {
            value = parse_expression(p);
        }
        zsp_ast_node_t *const item = new_node(p, ZSP_AST_BITMASK_ITEM, istart);
        if (item != NULL) {
            item->u.bitmask_item.name       = iname;
            item->u.bitmask_item.value_expr = value;
        }
        if (!push_or_oom(p, &items, item)) {
            return NULL;
        }
        if (!eat(p, ZSP_TOK_COMMA)) {
            break;
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close bitmask body");
    (void)eat(p, ZSP_TOK_SEMICOLON);
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_BITMASK, start);
    if (n != NULL) {
        n->u.bitmask_decl.name      = name;
        n->u.bitmask_decl.base_type = base;
        zsp_ast_list_take(&items, &n->u.bitmask_decl.items, &n->u.bitmask_decl.n_items);
    }
    return n;
}

static zsp_ast_node_t *parse_subtype(parser_t *p,
                                     zsp_ast_node_t **tparams,
                                     size_t n_tparams)
{
    const uint32_t start = p->cur.offset;
    advance(p);    /* consume 'subtype' */
    zsp_ast_node_t *const ty = parse_type(p);
    zsp_ast_node_t *const name = parse_ident(p);
    /* `subtype TargetType Name<T>;` — template args after name. */
    if (check(p, ZSP_TOK_LT)) {
        if (!parse_template_params(p, &tparams, &n_tparams)) {
            return NULL;
        }
    }
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after subtype");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_SUBTYPE, start);
    if (n != NULL) {
        n->u.subtype.template_params   = tparams;
        n->u.subtype.n_template_params = n_tparams;
        n->u.subtype.target_type       = ty;
        n->u.subtype.alias_name        = name;
    }
    return n;
}

static zsp_ast_node_t *parse_const(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);    /* consume 'const' */
    zsp_ast_node_t *const ty = parse_type(p);
    zsp_ast_node_t *const name = parse_ident(p);
    (void)expect(p, ZSP_TOK_ASSIGN, "expected '=' in const declaration");
    zsp_ast_node_t *const value = parse_expression(p);
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after const declaration");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_CONST, start);
    if (n != NULL) {
        n->u.const_decl.type       = ty;
        n->u.const_decl.name       = name;
        n->u.const_decl.value_expr = value;
    }
    return n;
}

static zsp_ast_node_t *parse_instantiate(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);    /* 'instantiate' */
    zsp_ast_node_t *const target = parse_type(p);
    zsp_ast_node_t *const name = parse_ident(p);
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after instantiate");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_INSTANTIATE, start);
    if (n != NULL) {
        n->u.instantiate.name   = name;
        n->u.instantiate.target = target;
    }
    return n;
}

static zsp_ast_node_t *parse_rule_group(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);    /* 'rule_group' */
    zsp_ast_node_t *const name = parse_ident(p);
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' after rule_group name");
    zsp_ast_list_t rules;
    zsp_ast_list_init(&rules, p->arena);
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t rstart = p->cur.offset;
        (void)expect(p, ZSP_TOK_KW_RULE, "expected 'rule' in rule_group body");
        zsp_ast_node_t *const rname = parse_expression(p);  /* rule id literal */
        zsp_ast_node_t *body = NULL;
        if (eat(p, ZSP_TOK_COLON)) {
            body = parse_expression(p);
        }
        (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after rule");
        zsp_ast_node_t *const r = new_node(p, ZSP_AST_RULE, rstart);
        if (r != NULL) {
            r->u.rule.id_expr   = rname;
            r->u.rule.body_expr = body;
            r->u.rule.name      = NULL;
        }
        if (!push_or_oom(p, &rules, r)) {
            return NULL;
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close rule_group");
    (void)eat(p, ZSP_TOK_SEMICOLON);
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_RULE_GROUP, start);
    if (n != NULL) {
        n->u.rule_group.name = name;
        zsp_ast_list_take(&rules, &n->u.rule_group.rules, &n->u.rule_group.n_rules);
    }
    return n;
}

/* ============================================================ */
/* SQL table / SQL database / service / pubsub                    */
/* ============================================================ */

static zsp_ast_node_t *parse_sql_field(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    bool optional_flag = false;
    /* `sql_virtual` modifier on the field; consumed and ignored
     * for now (analyser/emitter will pick it up later). */
    (void)eat(p, ZSP_TOK_KW_SQL_VIRTUAL);
    if (eat(p, ZSP_TOK_KW_OPTIONAL)) {
        optional_flag = true;
    }
    (void)eat(p, ZSP_TOK_KW_SQL_VIRTUAL);   /* may also follow optional */
    zsp_ast_node_t *const ty = parse_type(p);
    if (ty == NULL) {
        return NULL;
    }
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    /* SQL constraint can be either `: expr` or `sql expr`. The
     * latter often appears as `sql "STR" + "STR" + ...` so we
     * parse a full expression after `sql`. */
    zsp_ast_node_t *constraint = NULL;
    if (eat(p, ZSP_TOK_COLON)) {
        constraint = parse_expression(p);
    }
    else if (eat(p, ZSP_TOK_KW_SQL)) {
        constraint = parse_expression(p);
    }
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after SQL field");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_SQL_FIELD, start);
    if (n != NULL) {
        n->u.sql_field.type             = ty;
        n->u.sql_field.name             = name;
        n->u.sql_field.constraint_expr  = constraint;
        n->u.sql_field.optional         = optional_flag;
    }
    return n;
}

static zsp_ast_node_t *parse_sql_constraint_decl(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    /* upstream uses `sql` as the lead-in for table-level constraints */
    advance(p);  /* consume 'sql' */
    zsp_ast_node_t *const expr = parse_expression(p);
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after SQL constraint");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_SQL_CONSTRAINT, start);
    if (n != NULL) {
        n->u.sql_constraint.expr = expr;
    }
    return n;
}

static zsp_ast_node_t *parse_sql_table(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    bool virtual_flag = false;
    /* `sql_virtual` is a separate top-level keyword in upstream. */
    if (eat(p, ZSP_TOK_KW_SQL_VIRTUAL)) {
        virtual_flag = true;
    }
    advance(p);   /* consume 'sql_table' */
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    /* sql_table may carry template parameters: `sql_table T<U>`. */
    zsp_ast_node_t **sql_tparams   = NULL;
    size_t            sql_n_tparams = 0U;
    if (check(p, ZSP_TOK_LT)) {
        if (!parse_template_params(p, &sql_tparams, &sql_n_tparams)) {
            return NULL;
        }
    }
    /* `using IDENT` clause is allowed both as `sql_table NAME using X`
     * (e.g. for SQLite fts3/fts5 modules) and as the
     * `sql_virtual sql_table NAME using X` form. Accept either. */
    zsp_ast_node_t *virtual_using = NULL;
    if (eat(p, ZSP_TOK_KW_USING)) {
        virtual_using = parse_ident(p);
    }
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' to open SQL table body");

    zsp_ast_list_t fields;
    zsp_ast_list_t constraints;
    zsp_ast_list_init(&fields, p->arena);
    zsp_ast_list_init(&constraints, p->arena);
    bool without_rowid = false;
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t before = p->cur.offset;
        if (check(p, ZSP_TOK_KW_SQL)) {
            zsp_ast_node_t *const c = parse_sql_constraint_decl(p);
            if (c != NULL) {
                (void)push_or_oom(p, &constraints, c);
            }
        }
        else if (check(p, ZSP_TOK_KW_SQL_WITHOUT_ROWID)) {
            advance(p);
            (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after sql_without_rowid");
            without_rowid = true;
        }
        else {
            zsp_ast_node_t *const f = parse_sql_field(p);
            if (f != NULL) {
                (void)push_or_oom(p, &fields, f);
            }
        }
        if (p->cur.offset == before) {
            advance(p);
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close SQL table body");
    (void)eat(p, ZSP_TOK_SEMICOLON);

    zsp_ast_node_t *const n = new_node(p, ZSP_AST_SQL_TABLE, start);
    if (n != NULL) {
        n->u.sql_table.name              = name;
        n->u.sql_table.template_params   = sql_tparams;
        n->u.sql_table.n_template_params = sql_n_tparams;
        zsp_ast_list_take(&fields,      &n->u.sql_table.fields,
                          &n->u.sql_table.n_fields);
        zsp_ast_list_take(&constraints, &n->u.sql_table.constraints,
                          &n->u.sql_table.n_constraints);
        n->u.sql_table.virtual_       = virtual_flag;
        n->u.sql_table.without_rowid  = without_rowid;
        n->u.sql_table.virtual_using  = virtual_using;
    }
    return n;
}

static zsp_ast_node_t *parse_sql_database(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);   /* 'sql_database' */
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' to open SQL database body");

    zsp_ast_list_t refs;
    zsp_ast_list_init(&refs, p->arena);
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t before = p->cur.offset;
        const uint32_t rstart = p->cur.offset;
        zsp_ast_node_t *const ty = parse_type(p);
        zsp_ast_node_t *const fname = (ty != NULL) ? parse_ident(p) : NULL;
        (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after SQL database table reference");
        if ((ty != NULL) && (fname != NULL)) {
            zsp_ast_node_t *const r = new_node(p, ZSP_AST_SQL_TABLE_REF, rstart);
            if (r != NULL) {
                r->u.sql_table_ref.type = ty;
                r->u.sql_table_ref.name = fname;
                (void)push_or_oom(p, &refs, r);
            }
        }
        if (p->cur.offset == before) {
            advance(p);
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close SQL database body");
    (void)eat(p, ZSP_TOK_SEMICOLON);

    zsp_ast_node_t *const n = new_node(p, ZSP_AST_SQL_DATABASE, start);
    if (n != NULL) {
        n->u.sql_database.name = name;
        zsp_ast_list_take(&refs, &n->u.sql_database.table_refs,
                          &n->u.sql_database.n_table_refs);
    }
    return n;
}

static zsp_ast_node_t *parse_service_method(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    zsp_ast_node_t *const return_type = parse_type(p);
    if (return_type == NULL) {
        return NULL;
    }
    zsp_ast_node_t *const name = parse_ident(p);
    (void)expect(p, ZSP_TOK_LPAREN, "expected '(' after service method name");
    zsp_ast_node_t *const request_type = parse_type(p);
    (void)expect(p, ZSP_TOK_RPAREN, "expected ')' after service method request type");
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after service method");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_SERVICE_METHOD, start);
    if (n != NULL) {
        n->u.service_method.return_type  = return_type;
        n->u.service_method.name         = name;
        n->u.service_method.request_type = request_type;
    }
    return n;
}

static zsp_ast_node_t *parse_service(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);   /* 'service' */
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' to open service body");
    zsp_ast_list_t methods;
    zsp_ast_list_init(&methods, p->arena);
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t before = p->cur.offset;
        zsp_ast_node_t *const m = parse_service_method(p);
        if (m != NULL) {
            (void)push_or_oom(p, &methods, m);
        }
        if (p->cur.offset == before) {
            advance(p);
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close service body");
    (void)eat(p, ZSP_TOK_SEMICOLON);
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_SERVICE, start);
    if (n != NULL) {
        n->u.service.name = name;
        zsp_ast_list_take(&methods, &n->u.service.methods,
                          &n->u.service.n_methods);
    }
    return n;
}

static zsp_ast_node_t *parse_pubsub_message(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    /* Upstream grammar: `(publish|subscribe|pubsub) topic(expr) Type name;`
     * — the directionality keyword comes BEFORE `topic`. Accept
     * both orderings for resilience (some older schemas write
     * `topic(...) publish ...`). */
    bool publish = false;
    bool subscribe = false;
    while (check(p, ZSP_TOK_KW_PUBLISH) || check(p, ZSP_TOK_KW_SUBSCRIBE)
            || check(p, ZSP_TOK_KW_PUBSUB)) {
        if (eat(p, ZSP_TOK_KW_PUBLISH))   publish = true;
        else if (eat(p, ZSP_TOK_KW_SUBSCRIBE)) subscribe = true;
        else if (eat(p, ZSP_TOK_KW_PUBSUB)) {
            publish = true;
            subscribe = true;
        }
    }
    if (!expect(p, ZSP_TOK_KW_TOPIC, "expected 'topic' in pubsub body")) {
        return NULL;
    }
    (void)expect(p, ZSP_TOK_LPAREN, "expected '(' after 'topic'");
    zsp_ast_node_t *const topic_expr = parse_expression(p);
    (void)expect(p, ZSP_TOK_RPAREN, "expected ')' after topic expression");
    /* Trailing form: pub/sub keyword after the topic clause. */
    while (check(p, ZSP_TOK_KW_PUBLISH) || check(p, ZSP_TOK_KW_SUBSCRIBE)) {
        if (eat(p, ZSP_TOK_KW_PUBLISH))   publish = true;
        if (eat(p, ZSP_TOK_KW_SUBSCRIBE)) subscribe = true;
    }
    zsp_ast_node_t *const ty   = parse_type(p);
    zsp_ast_node_t *const name = parse_ident(p);
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after pubsub message");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_PUBSUB_MESSAGE, start);
    if (n != NULL) {
        n->u.pubsub_message.topic_expr = topic_expr;
        n->u.pubsub_message.type       = ty;
        n->u.pubsub_message.name       = name;
        n->u.pubsub_message.publish    = publish;
        n->u.pubsub_message.subscribe  = subscribe;
    }
    return n;
}

static zsp_ast_node_t *parse_pubsub(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);   /* 'pubsub' */
    zsp_ast_node_t *const name = parse_ident(p);
    if (name == NULL) {
        return NULL;
    }
    (void)expect(p, ZSP_TOK_LBRACE, "expected '{' to open pubsub body");
    zsp_ast_list_t msgs;
    zsp_ast_list_init(&msgs, p->arena);
    while (!check(p, ZSP_TOK_RBRACE) && !check(p, ZSP_TOK_EOF)) {
        const uint32_t before = p->cur.offset;
        zsp_ast_node_t *const m = parse_pubsub_message(p);
        if (m != NULL) {
            (void)push_or_oom(p, &msgs, m);
        }
        if (p->cur.offset == before) {
            advance(p);
        }
    }
    (void)expect(p, ZSP_TOK_RBRACE, "expected '}' to close pubsub body");
    (void)eat(p, ZSP_TOK_SEMICOLON);
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_PUBSUB, start);
    if (n != NULL) {
        n->u.pubsub.name = name;
        zsp_ast_list_take(&msgs, &n->u.pubsub.messages,
                          &n->u.pubsub.n_messages);
    }
    return n;
}

/* ============================================================ */
/* Top-level declarations dispatch                                */
/* ============================================================ */

static zsp_ast_node_t *parse_decl(parser_t *p, zsp_ast_node_t **opt_anno)
{
    (void)opt_anno;

    /* Template params are parsed AFTER the type name inside each
     * compound (per upstream grammar: `struct Name<T> (params) {...}`).
     * No tparams collected at this level. */

    if (check(p, ZSP_TOK_KW_STRUCT)) {
        return parse_struct_or_union(p, NULL, 0U, false);
    }
    if (check(p, ZSP_TOK_KW_UNION)) {
        return parse_struct_or_union(p, NULL, 0U, true);
    }
    if (check(p, ZSP_TOK_KW_CHOICE)) {
        return parse_choice(p, NULL, 0U);
    }
    if (check(p, ZSP_TOK_KW_SUBTYPE)) {
        return parse_subtype(p, NULL, 0U);
    }

    if (check(p, ZSP_TOK_KW_ENUM))         { return parse_enum_decl(p); }
    if (check(p, ZSP_TOK_KW_BITMASK))      { return parse_bitmask(p); }
    if (check(p, ZSP_TOK_KW_CONST))        { return parse_const(p); }
    if (check(p, ZSP_TOK_KW_INSTANTIATE))  { return parse_instantiate(p); }
    if (check(p, ZSP_TOK_KW_RULE_GROUP))   { return parse_rule_group(p); }
    if (check(p, ZSP_TOK_KW_SERVICE))      { return parse_service(p); }
    if (check(p, ZSP_TOK_KW_PUBSUB))       { return parse_pubsub(p); }
    if (check(p, ZSP_TOK_KW_SQL_TABLE) || check(p, ZSP_TOK_KW_SQL_VIRTUAL)) {
        return parse_sql_table(p);
    }
    if (check(p, ZSP_TOK_KW_SQL_DATABASE)) { return parse_sql_database(p); }

    emit_error_at_cur(p, "expected top-level declaration");
    return NULL;
}

/* ============================================================ */
/* Package + imports + compilation unit                          */
/* ============================================================ */

static zsp_ast_node_t *parse_compat_version(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);     /* 'zserio_compatibility_version' */
    /* Upstream grammar: COMPAT_VERSION LPAREN STRING_LITERAL RPAREN SEMICOLON.
     * Accept both `compat(... );` and the older `compat "..."; ` shape
     * for forward compatibility. */
    const bool parens = eat(p, ZSP_TOK_LPAREN);
    zsp_ast_node_t *vstr = NULL;
    if (check(p, ZSP_TOK_STRING_LIT)) {
        vstr = new_node_from_tok(p, ZSP_AST_EXPR_STRING_LIT, &p->cur);
        if (vstr != NULL) {
            vstr->u.string_lit.span_off = p->cur.offset;
            vstr->u.string_lit.span_len = p->cur.length;
        }
        advance(p);
    }
    else {
        emit_error_at_cur(p, "expected version string literal");
    }
    if (parens) {
        (void)expect(p, ZSP_TOK_RPAREN, "expected ')' after compatibility version");
    }
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after compatibility version");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_COMPAT_VERSION, start);
    if (n != NULL) {
        n->u.compat_version.version_string = vstr;
    }
    return n;
}

static zsp_ast_node_t *parse_package_decl(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);     /* 'package' */
    zsp_ast_node_t *const name = parse_dotted_name(p);
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after package declaration");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_PACKAGE, start);
    if (n != NULL) {
        n->u.package.name = name;
    }
    return n;
}

static zsp_ast_node_t *parse_import_decl(parser_t *p)
{
    const uint32_t start = p->cur.offset;
    advance(p);     /* 'import' */
    zsp_ast_node_t *const path = parse_dotted_name(p);
    bool wildcard = false;
    if ((p->cur.kind == ZSP_TOK_DOT) && (p->peek.kind == ZSP_TOK_STAR)) {
        advance(p);   /* dot */
        advance(p);   /* star */
        wildcard = true;
    }
    (void)expect(p, ZSP_TOK_SEMICOLON, "expected ';' after import declaration");
    zsp_ast_node_t *const n = new_node(p, ZSP_AST_IMPORT, start);
    if (n != NULL) {
        n->u.import.name     = path;
        n->u.import.wildcard = wildcard;
    }
    return n;
}

zserio_status_t zsp_parse_unit(zsp_source_t          *src,
                               zsp_file_id_t          file_id,
                               zsp_arena_t           *arena,
                               zsp_diag_collector_t  *diag,
                               zsp_ast_node_t       **out_unit)
{
    if ((src == NULL) || (arena == NULL) || (diag == NULL)
            || (out_unit == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    *out_unit = NULL;

    parser_t p;
    ZSERIO_TRY(parser_init(&p, src, file_id, arena, diag));

    const uint32_t unit_start = 0U;

    /* Optional compatibility-version directive at the very top. */
    zsp_ast_node_t *compat_version = NULL;
    if (check(&p, ZSP_TOK_KW_COMPAT_VERSION)) {
        compat_version = parse_compat_version(&p);
    }

    /* Package declaration. Upstream allows package-less files in
     * a few contexts; we accept that too. */
    zsp_ast_node_t *package = NULL;
    if (check(&p, ZSP_TOK_KW_PACKAGE)) {
        package = parse_package_decl(&p);
    }

    /* Imports. */
    zsp_ast_list_t imports;
    zsp_ast_list_init(&imports, p.arena);
    while (check(&p, ZSP_TOK_KW_IMPORT)) {
        zsp_ast_node_t *const im = parse_import_decl(&p);
        if (!push_or_oom(&p, &imports, im)) {
            return ZSERIO_ERR_OUT_OF_MEMORY;
        }
    }

    /* Top-level declarations. */
    zsp_ast_list_t decls;
    zsp_ast_list_init(&decls, p.arena);
    while (!check(&p, ZSP_TOK_EOF)) {
        zsp_ast_node_t *const d = parse_decl(&p, NULL);
        if (d == NULL) {
            /* Recover at next top-level keyword. */
            static const zsp_token_kind_t sync[] = {
                ZSP_TOK_KW_STRUCT, ZSP_TOK_KW_UNION, ZSP_TOK_KW_CHOICE,
                ZSP_TOK_KW_ENUM, ZSP_TOK_KW_BITMASK, ZSP_TOK_KW_SUBTYPE,
                ZSP_TOK_KW_CONST, ZSP_TOK_KW_INSTANTIATE,
                ZSP_TOK_KW_RULE_GROUP, ZSP_TOK_KW_SERVICE,
                ZSP_TOK_KW_PUBSUB, ZSP_TOK_KW_SQL_TABLE,
                ZSP_TOK_KW_SQL_VIRTUAL, ZSP_TOK_KW_SQL_DATABASE,
            };
            skip_to_any(&p, sync, sizeof(sync) / sizeof(sync[0]));
            if (p.oom) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        if (!push_or_oom(&p, &decls, d)) {
            return ZSERIO_ERR_OUT_OF_MEMORY;
        }
    }

    if (p.oom) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }

    zsp_ast_node_t *const unit = zsp_ast_new(arena, ZSP_AST_UNIT, file_id,
                                             unit_start,
                                             (uint32_t)p.cur.offset);
    if (unit == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    unit->u.unit.package        = package;
    unit->u.unit.compat_version = compat_version;
    zsp_ast_list_take(&imports, &unit->u.unit.imports, &unit->u.unit.n_imports);
    zsp_ast_list_take(&decls,   &unit->u.unit.decls,   &unit->u.unit.n_decls);
    *out_unit = unit;
    return ZSERIO_OK;
}
