/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zsparser/emit.h"

#include <string.h>

#include "zsparser/sema.h"

/*
 * C code emitter. Hand-written, per Q3. Each top-level
 * declaration emits a `(.h, .c)` pair. The .h carries the
 * struct definition + function prototypes; the .c carries the
 * function bodies (read/write/init/free).
 *
 * Buffer management: each output file is built into a per-file
 * arena-allocated growable buffer, then handed to the caller's
 * write callback when complete. Buffers don't outlive the
 * emit call.
 *
 * No varargs (MISRA Rule 17.1) — typed `emit_*` helpers only.
 * No recursion in the emit functions themselves (each is a
 * straight-line per-AST-kind handler); the AST tree walk uses
 * an iterative worklist where applicable.
 */

/* ----- Output buffer ----------------------------------------- */

typedef struct buf {
    zsp_arena_t *arena;
    char        *bytes;
    size_t       len;
    size_t       cap;
    bool         oom;
    /* When non-NULL, IDENT references to FIELDs / PARAMETERs
     * are emitted as `<this_override>.<name>` instead of
     * `self-><name>`. Used to inline a member-function call
     * `<recv>.<func>()` by substituting `<recv>` for `self->`
     * in the function body. The owner is responsible for
     * push/pop semantics (save old, set new, emit, restore). */
    const char  *this_override;
} buf_t;

static void buf_init(buf_t *b, zsp_arena_t *arena)
{
    b->arena = arena;
    b->bytes = NULL;
    b->len   = 0U;
    b->cap   = 0U;
    b->oom   = false;
    b->this_override = NULL;
}

static void buf_grow(buf_t *b, size_t needed)
{
    if ((b->len + needed) <= b->cap) {
        return;
    }
    size_t new_cap = (b->cap == 0U) ? 1024U : (b->cap * 2U);
    while (new_cap < (b->len + needed)) {
        new_cap *= 2U;
    }
    char *const grown = (char *)zsp_arena_alloc(b->arena, new_cap, 1U);
    if (grown == NULL) {
        b->oom = true;
        return;
    }
    if ((b->bytes != NULL) && (b->len > 0U)) {
        (void)memcpy(grown, b->bytes, b->len);
    }
    b->bytes = grown;
    b->cap   = new_cap;
}

static void emit_bytes(buf_t *b, const char *src, size_t len)
{
    buf_grow(b, len);
    if (b->oom) {
        return;
    }
    if (len > 0U) {
        (void)memcpy(&b->bytes[b->len], src, len);
        b->len += len;
    }
}

static void emit_lit(buf_t *b, const char *s)
{
    emit_bytes(b, s, strlen(s));
}

static void emit_span(buf_t *b, const uint8_t *src, size_t len)
{
    emit_bytes(b, (const char *)src, len);
}

static void emit_uint(buf_t *b, uint64_t v)
{
    if (v == 0U) {
        emit_lit(b, "0");
        return;
    }
    char tmp[24];
    size_t n = 0U;
    while ((v > 0U) && (n < sizeof(tmp))) {
        tmp[n] = (char)('0' + (char)(v % 10U));
        v /= 10U;
        n += 1U;
    }
    /* Reverse into the buffer. */
    char out[24];
    for (size_t i = 0U; i < n; ++i) {
        out[i] = tmp[n - 1U - i];
    }
    emit_bytes(b, out, n);
}

/* ----- Naming helpers --------------------------------------- */

/* Forward decls: emit_field_c_type uses emit_type_id, which is
 * defined alongside other name helpers further below. */
static bool emit_type_id(buf_t *b, const zsp_ast_node_t *decl,
                         const zsp_source_t *src);

/* Peel SUBTYPE / INSTANTIATE aliases so call-site emission
 * lands on the concrete decl that actually owns the `_init`,
 * `_read`, `_write`, `_free` symbols. */
static const zsp_ast_node_t *peel_alias(const zsp_ast_node_t *decl)
{
    while (decl != NULL) {
        if ((decl->kind == ZSP_AST_SUBTYPE)
                && (decl->u.subtype.target_type != NULL)
                && (decl->u.subtype.target_type->resolved != NULL)) {
            decl = decl->u.subtype.target_type->resolved;
            continue;
        }
        if ((decl->kind == ZSP_AST_INSTANTIATE)
                && (decl->u.instantiate.target != NULL)
                && (decl->u.instantiate.target->resolved != NULL)) {
            decl = decl->u.instantiate.target->resolved;
            continue;
        }
        break;
    }
    return decl;
}
static void emit_pkg_prefix(buf_t *b, const zsp_ast_node_t *decl,
                            const zsp_source_t *src);
/* Choice case helpers are reused by the union (struct/union)
 * emitter to switch on the tag, but they're defined further down
 * in the file with the rest of the choice plumbing. */
static void emit_choice_case_read(buf_t *b, const zsp_ast_node_t *f,
                                  const zsp_source_t *src);
static void emit_choice_case_write(buf_t *b, const zsp_ast_node_t *f,
                                   const zsp_source_t *src);
static void emit_choice_case_free(buf_t *b, const zsp_ast_node_t *f,
                                  const zsp_source_t *src);

/* Convert UpperCamelCase or mixedCamel to snake_case in-place
 * into the buffer. Doesn't lowercase letters that aren't ASCII
 * upper; doesn't handle non-ASCII. */
static void emit_snake(buf_t *b, const uint8_t *src, size_t len)
{
    /* Insert '_' before an upper-case letter when:
     *   - the previous char is lower-case or a digit, OR
     *   - the previous char is upper AND the next is lower (acronym
     *     end: "UInt8" → "u_int8", "HTMLParser" → "html_parser").
     * Matches the upstream zserio Java C generator convention,
     * which the xvalidation harnesses are written against. */
    for (size_t i = 0U; i < len; ++i) {
        const uint8_t c = src[i];
        if ((c >= (uint8_t)'A') && (c <= (uint8_t)'Z')) {
            if (i > 0U) {
                const uint8_t prev = src[i - 1U];
                const bool prev_lower_or_digit =
                        ((prev >= (uint8_t)'a') && (prev <= (uint8_t)'z'))
                        || ((prev >= (uint8_t)'0') && (prev <= (uint8_t)'9'));
                const bool prev_upper =
                        (prev >= (uint8_t)'A') && (prev <= (uint8_t)'Z');
                const bool next_lower = ((i + 1U) < len)
                        && (src[i + 1U] >= (uint8_t)'a')
                        && (src[i + 1U] <= (uint8_t)'z');
                if (prev_lower_or_digit || (prev_upper && next_lower)) {
                    emit_lit(b, "_");
                }
            }
            const char lc = (char)(c - (uint8_t)'A' + (uint8_t)'a');
            emit_bytes(b, &lc, 1U);
        }
        else {
            emit_bytes(b, (const char *)&c, 1U);
        }
    }
}

/* Snake-case into a raw buffer (same boundary rule as emit_snake).
 * Used by the slot-expression builders for field access in
 * generated function bodies. Returns the number of bytes
 * written. The caller must guarantee out has room for 2*len. */
static size_t snake_into(char *out, size_t pos, const uint8_t *src, size_t len)
{
    for (size_t i = 0U; i < len; ++i) {
        const uint8_t c = src[i];
        if ((c >= (uint8_t)'A') && (c <= (uint8_t)'Z')) {
            if (i > 0U) {
                const uint8_t prev = src[i - 1U];
                const bool prev_lower_or_digit =
                        ((prev >= (uint8_t)'a') && (prev <= (uint8_t)'z'))
                        || ((prev >= (uint8_t)'0') && (prev <= (uint8_t)'9'));
                const bool prev_upper =
                        (prev >= (uint8_t)'A') && (prev <= (uint8_t)'Z');
                const bool next_lower = ((i + 1U) < len)
                        && (src[i + 1U] >= (uint8_t)'a')
                        && (src[i + 1U] <= (uint8_t)'z');
                if (prev_lower_or_digit || (prev_upper && next_lower)) {
                    out[pos++] = '_';
                }
            }
            out[pos++] = (char)(c - 'A' + 'a');
        }
        else {
            out[pos++] = (char)c;
        }
    }
    return pos;
}

/* Same as emit_snake but uppercases letters. Used to mangle an
 * enum/bitmask/union name into the upper-snake prefix that
 * decorates its items (e.g. `BasicColor` → `BASIC_COLOR`). */
static void emit_upper_snake(buf_t *b, const uint8_t *src, size_t len)
{
    /* Same boundary rule as emit_snake (look-ahead acronym
     * detection) but emits letters uppercased. */
    for (size_t i = 0U; i < len; ++i) {
        const uint8_t c = src[i];
        if ((c >= (uint8_t)'A') && (c <= (uint8_t)'Z')) {
            if (i > 0U) {
                const uint8_t prev = src[i - 1U];
                const bool prev_lower_or_digit =
                        ((prev >= (uint8_t)'a') && (prev <= (uint8_t)'z'))
                        || ((prev >= (uint8_t)'0') && (prev <= (uint8_t)'9'));
                const bool prev_upper =
                        (prev >= (uint8_t)'A') && (prev <= (uint8_t)'Z');
                const bool next_lower = ((i + 1U) < len)
                        && (src[i + 1U] >= (uint8_t)'a')
                        && (src[i + 1U] <= (uint8_t)'z');
                if (prev_lower_or_digit || (prev_upper && next_lower)) {
                    emit_lit(b, "_");
                }
            }
            emit_bytes(b, (const char *)&c, 1U);
        }
        else if ((c >= (uint8_t)'a') && (c <= (uint8_t)'z')) {
            const char uc = (char)(c - (uint8_t)'a' + (uint8_t)'A');
            emit_bytes(b, &uc, 1U);
        }
        else {
            emit_bytes(b, (const char *)&c, 1U);
        }
    }
}

/* Extract name span from an IDENT node. */
static bool ident_span(const zsp_source_t *src, const zsp_ast_node_t *n,
                       const uint8_t **out, size_t *out_len)
{
    if ((n == NULL) || (n->kind != ZSP_AST_EXPR_IDENT)) {
        return false;
    }
    const zsp_file_t *f = NULL;
    if (!zsp_source_get(src, n->file_id, &f)) {
        return false;
    }
    *out     = &f->bytes[n->u.ident.span_off];
    *out_len = (size_t)n->u.ident.span_len;
    return true;
}

/* ----- Builtin → C type-name table -------------------------- */

static const char *builtin_c_type(zsp_builtin_code_t code)
{
    switch (code) {
    case ZSP_TB_BOOL:       return "bool";
    case ZSP_TB_INT8:       return "int8_t";
    case ZSP_TB_INT16:      return "int16_t";
    case ZSP_TB_INT32:      return "int32_t";
    case ZSP_TB_INT64:      return "int64_t";
    case ZSP_TB_UINT8:      return "uint8_t";
    case ZSP_TB_UINT16:     return "uint16_t";
    case ZSP_TB_UINT32:     return "uint32_t";
    case ZSP_TB_UINT64:     return "uint64_t";
    case ZSP_TB_VARINT16:   return "int16_t";
    case ZSP_TB_VARINT32:   return "int32_t";
    case ZSP_TB_VARINT:
    case ZSP_TB_VARINT64:   return "int64_t";
    case ZSP_TB_VARUINT16:  return "uint16_t";
    case ZSP_TB_VARUINT32:  return "uint32_t";
    case ZSP_TB_VARUINT:
    case ZSP_TB_VARUINT64:  return "uint64_t";
    case ZSP_TB_VARSIZE:    return "uint32_t";
    case ZSP_TB_FLOAT16:    return "float";
    case ZSP_TB_FLOAT32:    return "float";
    case ZSP_TB_FLOAT64:    return "double";
    case ZSP_TB_STRING:     return "zserio_string_t";
    case ZSP_TB_BYTES:      return "zserio_bytes_t";
    case ZSP_TB_EXTERN:     return "zserio_extern_t";
    case ZSP_TB_BIT_FIELD:  return "uint64_t";
    case ZSP_TB_INT_FIELD:  return "int64_t";
    default:                return "void";
    }
}

/* Runtime read function for a builtin type — what the generated
 * .c file calls to read one element. */
static const char *builtin_read_fn(zsp_builtin_code_t code)
{
    switch (code) {
    case ZSP_TB_BOOL:       return "zserio_read_bool";
    case ZSP_TB_INT8:       return "zserio_read_int8";
    case ZSP_TB_INT16:      return "zserio_read_int16";
    case ZSP_TB_INT32:      return "zserio_read_int32";
    case ZSP_TB_INT64:      return "zserio_read_int64";
    case ZSP_TB_UINT8:      return "zserio_read_uint8";
    case ZSP_TB_UINT16:     return "zserio_read_uint16";
    case ZSP_TB_UINT32:     return "zserio_read_uint32";
    case ZSP_TB_UINT64:     return "zserio_read_uint64";
    case ZSP_TB_FLOAT16:    return "zserio_read_float16";
    case ZSP_TB_FLOAT32:    return "zserio_read_float32";
    case ZSP_TB_FLOAT64:    return "zserio_read_float64";
    case ZSP_TB_STRING:     return "zserio_read_string";
    case ZSP_TB_BYTES:      return "zserio_read_bytes";
    case ZSP_TB_EXTERN:     return "zserio_read_extern";
    case ZSP_TB_VARSIZE:    return "zserio_read_varsize";
    case ZSP_TB_VARINT:     return "zserio_read_varint";
    case ZSP_TB_VARINT16:   return "zserio_read_varint16";
    case ZSP_TB_VARINT32:   return "zserio_read_varint32";
    case ZSP_TB_VARINT64:   return "zserio_read_varint64";
    case ZSP_TB_VARUINT:    return "zserio_read_varuint";
    case ZSP_TB_VARUINT16:  return "zserio_read_varuint16";
    case ZSP_TB_VARUINT32:  return "zserio_read_varuint32";
    case ZSP_TB_VARUINT64:  return "zserio_read_varuint64";
    default:                return "/*unsupported_read*/";
    }
}

static const char *builtin_write_fn(zsp_builtin_code_t code)
{
    switch (code) {
    case ZSP_TB_BOOL:       return "zserio_write_bool";
    case ZSP_TB_INT8:       return "zserio_write_int8";
    case ZSP_TB_INT16:      return "zserio_write_int16";
    case ZSP_TB_INT32:      return "zserio_write_int32";
    case ZSP_TB_INT64:      return "zserio_write_int64";
    case ZSP_TB_UINT8:      return "zserio_write_uint8";
    case ZSP_TB_UINT16:     return "zserio_write_uint16";
    case ZSP_TB_UINT32:     return "zserio_write_uint32";
    case ZSP_TB_UINT64:     return "zserio_write_uint64";
    case ZSP_TB_FLOAT16:    return "zserio_write_float16";
    case ZSP_TB_FLOAT32:    return "zserio_write_float32";
    case ZSP_TB_FLOAT64:    return "zserio_write_float64";
    case ZSP_TB_STRING:     return "zserio_write_string";
    case ZSP_TB_BYTES:      return "zserio_write_bytes";
    case ZSP_TB_EXTERN:     return "zserio_write_extern";
    case ZSP_TB_VARSIZE:    return "zserio_write_varsize";
    case ZSP_TB_VARINT:     return "zserio_write_varint";
    case ZSP_TB_VARINT16:   return "zserio_write_varint16";
    case ZSP_TB_VARINT32:   return "zserio_write_varint32";
    case ZSP_TB_VARINT64:   return "zserio_write_varint64";
    case ZSP_TB_VARUINT:    return "zserio_write_varuint";
    case ZSP_TB_VARUINT16:  return "zserio_write_varuint16";
    case ZSP_TB_VARUINT32:  return "zserio_write_varuint32";
    case ZSP_TB_VARUINT64:  return "zserio_write_varuint64";
    default:                return "/*unsupported_write*/";
    }
}

/* Whether the field type needs allocator-aware free (strings,
 * bytes, extern — anything that owns heap memory). */
static bool builtin_needs_free(zsp_builtin_code_t code)
{
    return (code == ZSP_TB_STRING) || (code == ZSP_TB_BYTES) || (code == ZSP_TB_EXTERN);
}

static const char *builtin_free_fn(zsp_builtin_code_t code)
{
    switch (code) {
    case ZSP_TB_STRING: return "zserio_string_free";
    case ZSP_TB_BYTES:  return "zserio_bytes_free";
    case ZSP_TB_EXTERN: return "zserio_extern_free";
    default:            return "";
    }
}

/* ----- Emit one compound's header + source ------------------- */

typedef struct emit_ctx {
    const zsp_ast_node_t *unit;
    zsp_arena_t          *arena;
    zsp_diag_collector_t *diag;
    const zsp_source_t   *source;
    zsp_emit_write_fn     write;
    void                 *write_userdata;
    const uint8_t        *pkg_name;
    size_t                pkg_name_len;
} emit_ctx_t;

/* Emit the type-spelling for a field type (used in struct member
 * declarations). Returns the builtin code if known, ZSP_TB_UNKNOWN
 * otherwise. Sets `*is_owned` to true for types that need a
 * `_free` call. */
static zsp_builtin_code_t emit_field_c_type(buf_t *b,
                                            const zsp_ast_node_t *type_node,
                                            const zsp_source_t *src,
                                            bool *is_owned)
{
    *is_owned = false;
    if (type_node == NULL) {
        emit_lit(b, "void");
        return ZSP_TB_UNKNOWN;
    }
    if (type_node->kind == ZSP_AST_TYPE_BUILTIN) {
        const zsp_builtin_code_t c = type_node->u.type_builtin.code;
        /* For sized bit/int fields, pick the smallest builtin
         * that fits the declared width so harnesses can assign
         * `uint8_t arr[N]` literally. Dynamic-width (width == 0)
         * stays on the 64-bit storage. */
        if ((c == ZSP_TB_BIT_FIELD) || (c == ZSP_TB_INT_FIELD)) {
            const uint32_t w = type_node->u.type_builtin.width;
            const bool is_signed = (c == ZSP_TB_INT_FIELD);
            if (w > 0U && w <= 8U)        emit_lit(b, is_signed ? "int8_t"  : "uint8_t");
            else if (w > 0U && w <= 16U)  emit_lit(b, is_signed ? "int16_t" : "uint16_t");
            else if (w > 0U && w <= 32U)  emit_lit(b, is_signed ? "int32_t" : "uint32_t");
            else                          emit_lit(b, is_signed ? "int64_t" : "uint64_t");
            *is_owned = false;
            return c;
        }
        emit_lit(b, builtin_c_type(c));
        *is_owned = builtin_needs_free(c);
        return c;
    }
    if (type_node->kind == ZSP_AST_TYPE_REF) {
        /* For named types, the emitted C is `<pkg>_<snake(name)>_t`.
         * Prefer the referenced decl's package when resolved; fall
         * back to the local name with no prefix if it didn't
         * resolve. */
        const zsp_ast_node_t *decl = type_node->resolved;
        if ((decl != NULL) && emit_type_id(b, decl, src)) {
            emit_lit(b, "_t");
        }
        else {
            const zsp_ast_node_t *name = type_node->u.type_ref.name;
            const zsp_ast_node_t *id   = name;
            if ((name != NULL) && (name->kind == ZSP_AST_DOTTED_NAME)) {
                id = name->u.dotted.parts[name->u.dotted.n_parts - 1U];
            }
            const uint8_t *bytes = NULL;
            size_t         len   = 0U;
            if (ident_span(src, id, &bytes, &len)) {
                emit_snake(b, bytes, len);
                emit_lit(b, "_t");
            }
            else {
                emit_lit(b, "void");
            }
        }
        /* Owned-ness for named types is decided by their resolved
         * shape; for now, treat as owned (caller can choose to
         * free or not). Conservative. */
        *is_owned = true;
        return ZSP_TB_UNKNOWN;
    }
    if (type_node->kind == ZSP_AST_TYPE_ARRAY) {
        /* Arrays are emitted as pointer + count fields by the
         * caller — this helper just produces the element type. */
        bool inner_owned = false;
        const zsp_builtin_code_t c = emit_field_c_type(b, type_node->u.type_array.element,
                                                       src, &inner_owned);
        *is_owned = true;  /* arrays are always owned (have a buffer to free) */
        return c;
    }
    emit_lit(b, "void");
    return ZSP_TB_UNKNOWN;
}

/* Emit the struct definition for a compound. Returns void; OOM
 * surfaces via buf.oom. */
static void emit_struct_decl(buf_t *b, const zsp_ast_node_t *cmpd,
                             const zsp_source_t *src)
{
    /* Use a `_s` tag suffix on the struct so the typedef can be
     * forward-declared (necessary for self-referential structs
     * like `struct Node { Node next[]; }`). The `_s` suffix
     * avoids C-keyword clashes for schemas with type names like
     * `Float`, `Double`, `Long`, etc. The name is package-
     * qualified to match the Java generator's identifier
     * convention. */
    const bool is_union = (cmpd->kind == ZSP_AST_UNION);

    emit_lit(b, "typedef struct ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_s ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t;\n");
    emit_lit(b, "struct ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_s {\n");

    /* Emit parameters first (parameterized types). */
    for (size_t i = 0U; i < cmpd->u.compound.n_params; ++i) {
        const zsp_ast_node_t *param = cmpd->u.compound.params[i];
        emit_lit(b, "    ");
        bool owned = false;
        (void)emit_field_c_type(b, param->u.parameter.type, src, &owned);
        emit_lit(b, " ");
        const uint8_t *pn = NULL;
        size_t         pl = 0U;
        if (ident_span(src, param->u.parameter.name, &pn, &pl)) {
            emit_snake(b, pn, pl);
        }
        emit_lit(b, ";  /* parameter */\n");
    }

    /* Unions carry a varsize tag on the wire. Emit it as the
     * runtime's varsize storage type (uint32). Members are then
     * placed in an anonymous `u` union so only the selected arm
     * is meaningful. For unions, the per-array `_count` sibling
     * lives OUTSIDE the union (alongside the tag) — the count
     * has to survive switching arms. */
    if (is_union) {
        emit_lit(b, "    uint32_t tag;\n");
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
            const zsp_ast_node_t *ty = m->u.field.type;
            const bool is_array = (ty != NULL) && (ty->kind == ZSP_AST_TYPE_ARRAY);
            if (!is_array) continue;
            const zsp_ast_node_t *len_expr = ty->u.type_array.length_expr;
            const zsp_sema_const_t *len_cv = (len_expr != NULL)
                    ? (const zsp_sema_const_t *)len_expr->computed : NULL;
            const bool is_fixed_array = (len_cv != NULL)
                    && (len_cv->kind == ZSP_CONST_UINT)
                    && (len_cv->v.u > 0U);
            if (is_fixed_array) continue;
            const uint8_t *fn = NULL;
            size_t         fl = 0U;
            if (!ident_span(src, m->u.field.name, &fn, &fl)) continue;
            emit_lit(b, "    size_t ");
            emit_snake(b, fn, fl);
            emit_lit(b, "_count;\n");
        }
        emit_lit(b, "    union {\n");
    }

    /* Emit fields. Self-referential fields (the field's type
     * resolves to the enclosing compound) must be emitted as
     * pointers; C doesn't allow recursive value-typed fields.
     * Optional fields (`optional T x;` or `T x if cond;`) get
     * a sibling `bool <name>_present` flag. */
    for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if (m->kind != ZSP_AST_FIELD) {
            continue;
        }
        const zsp_ast_node_t *ty = m->u.field.type;
        const bool is_array = (ty != NULL) && (ty->kind == ZSP_AST_TYPE_ARRAY);
        const zsp_ast_node_t *element_ty = is_array ? ty->u.type_array.element : ty;
        const bool is_self_recursive =
                !is_array
                && (element_ty != NULL)
                && (element_ty->kind == ZSP_AST_TYPE_REF)
                && (element_ty->resolved == cmpd);
        const bool has_present =
                m->u.field.optional || (m->u.field.if_clause != NULL);
        /* Fixed-size array if the length expression const-evals
         * to a concrete value. Emit as `T name[N];` so the harness
         * (and Java-generator parity) can index it directly. */
        const zsp_ast_node_t *len_expr = is_array
                ? ty->u.type_array.length_expr : NULL;
        const zsp_sema_const_t *len_cv = (len_expr != NULL)
                ? (const zsp_sema_const_t *)len_expr->computed : NULL;
        /* Only treat as fixed-storage when the const length is
         * non-zero; C99 forbids zero-element arrays, so the
         * degenerate `array[0]` case falls back to pointer +
         * count (always empty at runtime but legal). */
        const bool is_fixed_array = is_array && (len_cv != NULL)
                && (len_cv->kind == ZSP_CONST_UINT)
                && (len_cv->v.u > 0U);
        const uint64_t fixed_n = is_fixed_array ? len_cv->v.u : 0U;

        emit_lit(b, "    ");
        bool owned = false;
        (void)emit_field_c_type(b, element_ty, src, &owned);
        if ((is_array && !is_fixed_array) || is_self_recursive) {
            emit_lit(b, " *");
        }
        else {
            emit_lit(b, " ");
        }
        const uint8_t *fn = NULL;
        size_t         fl = 0U;
        if (ident_span(src, m->u.field.name, &fn, &fl)) {
            emit_snake(b, fn, fl);
        }
        if (is_fixed_array) {
            emit_lit(b, "[");
            emit_uint(b, fixed_n);
            emit_lit(b, "]");
        }
        emit_lit(b, ";\n");

        /* For struct fields, `_count` is a sibling at the same
         * scope as the array pointer. For union fields it lives
         * OUTSIDE the union (emitted above) so it survives arm
         * switches. */
        if (is_array && !is_fixed_array && !is_union) {
            emit_lit(b, "    size_t ");
            if (ident_span(src, m->u.field.name, &fn, &fl)) {
                emit_snake(b, fn, fl);
            }
            emit_lit(b, "_count;\n");
        }
        if (has_present) {
            emit_lit(b, "    bool ");
            if (ident_span(src, m->u.field.name, &fn, &fl)) {
                emit_snake(b, fn, fl);
            }
            emit_lit(b, "_present;\n");
        }
    }

    if (is_union) {
        emit_lit(b, "    } u;\n");
    }
    /* C99 forbids zero-member structs; insert a placeholder when
     * the compound has no parameters and no FIELD members (non-
     * field members like functions don't contribute storage). */
    if (!is_union) {
        bool any_storage = (cmpd->u.compound.n_params > 0U);
        for (size_t i = 0U; !any_storage && i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if (m != NULL && m->kind == ZSP_AST_FIELD) {
                any_storage = true;
            }
        }
        if (!any_storage) {
            emit_lit(b, "    char _zsp_empty;\n");
        }
    }
    emit_lit(b, "};\n");
}

/* Forward declarations of helpers defined later in the file
 * that the packing-protocol emitters need. */
typedef struct packed_elem packed_elem_t;
static bool packed_elem_describe(const zsp_ast_node_t *ty, packed_elem_t *out);
static zsp_builtin_code_t enum_underlying(const zsp_ast_node_t *type_ref);
static uint32_t enum_underlying_width(const zsp_ast_node_t *type_ref);
static void emit_element_type(buf_t *b, const zsp_ast_node_t *element_ty,
                              const zsp_source_t *src);
static void emit_field_read(buf_t *b, const zsp_ast_node_t *field,
                            const zsp_source_t *src);
static void emit_field_write(buf_t *b, const zsp_ast_node_t *field,
                             const zsp_source_t *src,
                             bool referenced_as_offset_source);

/* The packed-element descriptor is a small POD; declare here so
 * field_packing_class can reference it. The full struct body lives
 * with packed_elem_describe further down. */
struct packed_elem {
    uint32_t                num_bits;       /* 0 if dynamic */
    const zsp_ast_node_t   *width_expr;     /* set when num_bits==0 */
    bool                    is_signed;
    const char             *cast_type;      /* C type name */
};

/* Emit function prototypes for one compound. */
/* Maps a varint builtin to its runtime traits-struct name (NULL
 * if the builtin isn't a varint). The packed-array path uses
 * traits-driven delta contexts for variable-length integer
 * elements. */
static const char *varint_traits_name(zsp_builtin_code_t c, bool *is_signed_out)
{
    if (is_signed_out != NULL) *is_signed_out = false;
    switch (c) {
    case ZSP_TB_VARUINT16: return "zserio_delta_traits_varuint16";
    case ZSP_TB_VARUINT32: return "zserio_delta_traits_varuint32";
    case ZSP_TB_VARUINT64: return "zserio_delta_traits_varuint64";
    case ZSP_TB_VARUINT:   return "zserio_delta_traits_varuint";
    case ZSP_TB_VARSIZE:   return "zserio_delta_traits_varsize";
    case ZSP_TB_VARINT16:  if (is_signed_out) *is_signed_out = true; return "zserio_delta_traits_varint16";
    case ZSP_TB_VARINT32:  if (is_signed_out) *is_signed_out = true; return "zserio_delta_traits_varint32";
    case ZSP_TB_VARINT64:  if (is_signed_out) *is_signed_out = true; return "zserio_delta_traits_varint64";
    case ZSP_TB_VARINT:    if (is_signed_out) *is_signed_out = true; return "zserio_delta_traits_varint";
    default: return NULL;
    }
}

/* For a compound's FIELD, classify how it participates in the
 * packed-array protocol of the enclosing compound: 'p' if its
 * type is a packable fixed-width scalar (gets a delta context),
 * 'v' if it's a varint flavour (traits-driven delta context),
 * 'n' if it's a nested struct/union (gets a nested packing
 * context), 'x' if unpackable (regular read/write in the packed
 * path). Fields with arrays or self-references are treated as
 * unpackable for now. */
static char field_packing_class(const zsp_ast_node_t *field,
                                packed_elem_t *scalar_out)
{
    if ((field == NULL) || (field->kind != ZSP_AST_FIELD)) return 'x';
    const zsp_ast_node_t *ty = field->u.field.type;
    if (ty == NULL) return 'x';
    if (ty->kind == ZSP_AST_TYPE_ARRAY) return 'x';
    /* Peel SUBTYPE chains for classification. */
    while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
            && (ty->resolved != NULL)
            && (ty->resolved->kind == ZSP_AST_SUBTYPE)
            && (ty->resolved->u.subtype.target_type != NULL)) {
        ty = ty->resolved->u.subtype.target_type;
    }
    if (ty == NULL) return 'x';
    if ((ty->kind == ZSP_AST_TYPE_BUILTIN)
            && (varint_traits_name(ty->u.type_builtin.code, NULL) != NULL)) {
        if (scalar_out != NULL) {
            (void)memset(scalar_out, 0, sizeof(*scalar_out));
            (void)varint_traits_name(ty->u.type_builtin.code, &scalar_out->is_signed);
        }
        return 'v';
    }
    packed_elem_t pe = {0};
    if (packed_elem_describe(ty, &pe)) {
        if (scalar_out != NULL) *scalar_out = pe;
        return 'p';
    }
    if (ty->kind == ZSP_AST_TYPE_REF) {
        const zsp_ast_node_t *decl = peel_alias(ty->resolved);
        if ((decl != NULL)
                && ((decl->kind == ZSP_AST_STRUCT)
                    || (decl->kind == ZSP_AST_UNION))) {
            return 'n';
        }
    }
    return 'x';
}

/* Does this compound have ANY packable scalar field, or any
 * nested compound that itself does? Used to decide whether the
 * compound needs packing-context machinery. Unions are excluded
 * (per-field packing doesn't apply across arms — only one is
 * active per element). Compounds with offset/align directives
 * are also excluded since the packed read/write entry points
 * don't carry the outer `_off_pos_` variables those need. */
static bool compound_has_packable_field(const zsp_ast_node_t *cmpd)
{
    if ((cmpd == NULL) || (cmpd->kind != ZSP_AST_STRUCT)) {
        return false;
    }
    /* Skip if any field uses offset/align directives — the
     * packed read/write paths don't have outer-scope variables
     * for back-patching. */
    for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
        if ((m->u.field.offset_expr != NULL)
                || (m->u.field.align_expr != NULL)) {
            return false;
        }
    }
    for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
        const char cls = field_packing_class(m, NULL);
        if ((cls == 'p') || (cls == 'v')) return true;
        if (cls == 'n') {
            /* Recurse one level — nested compound's packability
             * propagates. (We don't deep-recurse to avoid
             * cycles; the nested compound's own emission will
             * handle deeper.) */
            const zsp_ast_node_t *ty = m->u.field.type;
            while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                    && (ty->resolved != NULL)
                    && (ty->resolved->kind == ZSP_AST_SUBTYPE)
                    && (ty->resolved->u.subtype.target_type != NULL)) {
                ty = ty->resolved->u.subtype.target_type;
            }
            if ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                    && (ty->resolved != NULL)
                    && (ty->resolved != cmpd)
                    && compound_has_packable_field(ty->resolved)) {
                return true;
            }
        }
    }
    return false;
}

/* Emit `<T>_packing_context_t` typedef body — one delta_context
 * per packable scalar field, one nested packing-context per
 * nested compound field. Skips unpackable fields. */
static void emit_packing_ctx_typedef(buf_t *b, const zsp_ast_node_t *cmpd,
                                     const zsp_source_t *src)
{
    if (!compound_has_packable_field(cmpd)) return;
    emit_lit(b, "typedef struct {\n");
    bool any = false;
    for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
        const char cls = field_packing_class(m, NULL);
        const uint8_t *fn = NULL;
        size_t         fl = 0U;
        if (!ident_span(src, m->u.field.name, &fn, &fl)) continue;
        if ((cls == 'p') || (cls == 'v')) {
            emit_lit(b, "    zserio_delta_context_t ");
            emit_snake(b, fn, fl);
            emit_lit(b, "_;\n");
            any = true;
        } else if (cls == 'n') {
            const zsp_ast_node_t *ty = m->u.field.type;
            while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                    && (ty->resolved != NULL)
                    && (ty->resolved->kind == ZSP_AST_SUBTYPE)
                    && (ty->resolved->u.subtype.target_type != NULL)) {
                ty = ty->resolved->u.subtype.target_type;
            }
            if ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                    && (ty->resolved != NULL)
                    && (ty->resolved != cmpd)
                    && compound_has_packable_field(ty->resolved)) {
                emit_lit(b, "    ");
                (void)emit_type_id(b, ty->resolved, src);
                emit_lit(b, "_packing_context_t ");
                emit_snake(b, fn, fl);
                emit_lit(b, "_;\n");
                any = true;
            }
        }
    }
    if (!any) {
        emit_lit(b, "    char _zsp_empty;\n");
    }
    emit_lit(b, "} ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_packing_context_t;\n\n");
}

static void emit_compound_protos(buf_t *b, const zsp_ast_node_t *cmpd,
                                 const zsp_source_t *src)
{
    emit_lit(b, "\nvoid ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_init(");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t *self);\n");

    /* Parameterised types get a setter for the parameter pack. */
    if (cmpd->u.compound.n_params > 0U) {
        emit_lit(b, "\nvoid ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_set_parameters(");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_t *self");
        for (size_t i = 0U; i < cmpd->u.compound.n_params; ++i) {
            const zsp_ast_node_t *p = cmpd->u.compound.params[i];
            emit_lit(b, ", ");
            bool owned = false;
            (void)emit_field_c_type(b, p->u.parameter.type, src, &owned);
            emit_lit(b, " ");
            const uint8_t *pn = NULL;
            size_t         pl = 0U;
            if (ident_span(src, p->u.parameter.name, &pn, &pl)) {
                emit_snake(b, pn, pl);
            }
        }
        emit_lit(b, ");\n");
    }

    emit_lit(b, "\nzserio_status_t ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_read(");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t *self, zserio_bitstream_reader_t *bs,\n"
                "        const zserio_allocator_t *alloc);\n");

    emit_lit(b, "\nzserio_status_t ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_write(const ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t *self, zserio_bitstream_writer_t *bs);\n");

    emit_lit(b, "\nvoid ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_free(");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t *self, const zserio_allocator_t *alloc);\n");

    /* Packing-protocol entry points (only if the compound has
     * at least one packable scalar / nested compound). */
    if (compound_has_packable_field(cmpd)) {
        emit_lit(b, "\nvoid ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_init_packing_context(");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_packing_context_t *ctx);\n");

        emit_lit(b, "\nvoid ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_observe_packing(");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_packing_context_t *ctx, const ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_t *self);\n");

        emit_lit(b, "\nzserio_status_t ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_read_packed(");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_t *self, zserio_bitstream_reader_t *bs,\n"
                    "        const zserio_allocator_t *alloc, ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_packing_context_t *ctx);\n");

        emit_lit(b, "\nzserio_status_t ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_write_packed(const ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_t *self, zserio_bitstream_writer_t *bs, ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_packing_context_t *ctx);\n");
    }
}

/* Compute the dotted package name for a top-level decl. The
 * sema repurposes `decl->resolved` on top-level decls to point
 * to their enclosing UNIT; we read the unit's package decl
 * here. Writes a `.`-separated lowercase path into `out`;
 * returns the length written. Empty for the default (package-
 * less) compilation unit. */
static size_t decl_package_path(const zsp_ast_node_t *decl,
                                const zsp_source_t *src,
                                char *out, size_t out_cap)
{
    out[0] = '\0';
    if ((decl == NULL) || (decl->resolved == NULL)) return 0U;
    const zsp_ast_node_t *unit = decl->resolved;
    if (unit->kind != ZSP_AST_UNIT) return 0U;
    if (unit->u.unit.package == NULL) return 0U;
    const zsp_ast_node_t *pname = unit->u.unit.package->u.package.name;
    if (pname == NULL) return 0U;

    size_t pos = 0U;
    const zsp_ast_node_t *const *parts = NULL;
    size_t n_parts = 0U;
    if (pname->kind == ZSP_AST_DOTTED_NAME) {
        parts   = (const zsp_ast_node_t * const *)pname->u.dotted.parts;
        n_parts = pname->u.dotted.n_parts;
    }
    else if (pname->kind == ZSP_AST_EXPR_IDENT) {
        const uint8_t *bytes = NULL;
        size_t         len   = 0U;
        if (ident_span(src, pname, &bytes, &len)) {
            const size_t copy = (len < (out_cap - 1U)) ? len : (out_cap - 1U);
            for (size_t i = 0U; i < copy; ++i) {
                const uint8_t c = bytes[i];
                out[pos++] = (char)(((c >= 'A') && (c <= 'Z'))
                        ? (c - 'A' + 'a') : c);
            }
            out[pos] = '\0';
        }
        return pos;
    }
    for (size_t i = 0U; i < n_parts; ++i) {
        const uint8_t *bytes = NULL;
        size_t         len   = 0U;
        if (!ident_span(src, parts[i], &bytes, &len)) continue;
        if ((i > 0U) && ((pos + 1U) < out_cap)) {
            out[pos++] = '/';
        }
        const size_t copy = (len < (out_cap - 1U - pos))
                ? len : (out_cap - 1U - pos);
        for (size_t k = 0U; k < copy; ++k) {
            const uint8_t c = bytes[k];
            out[pos++] = (char)(((c >= 'A') && (c <= 'Z'))
                    ? (c - 'A' + 'a') : c);
        }
    }
    out[pos] = '\0';
    return pos;
}

/* Same path as decl_package_path but joined with '_' instead of
 * '/', for use in C identifier prefixes (e.g. emitting
 * `expressions_full_enumeration_type_color_t`). Returns the
 * number of bytes written (excluding the trailing nul). */
static size_t decl_package_id(const zsp_ast_node_t *decl,
                              const zsp_source_t *src,
                              char *out, size_t out_cap)
{
    const size_t n = decl_package_path(decl, src, out, out_cap);
    for (size_t i = 0U; i < n; ++i) {
        if (out[i] == '/') out[i] = '_';
    }
    return n;
}

/* Emit `<pkg>_` (or nothing if the decl has no package). Used as
 * a prefix before each type identifier so the C names match the
 * upstream Java generator's naming convention. */
static void emit_pkg_prefix(buf_t *b, const zsp_ast_node_t *decl,
                            const zsp_source_t *src)
{
    char buf[1024];
    const size_t n = decl_package_id(decl, src, buf, sizeof(buf));
    if (n == 0U) return;
    emit_bytes(b, buf, n);
    emit_lit(b, "_");
}

/* Emit `<pkg>_<snake(name)>`. The lookup of the decl's name node
 * is per-decl-kind so the helper handles every top-level kind we
 * care about (struct, union, choice, enum, bitmask, subtype,
 * const, instantiate, sql-table, sql-database). Returns whether
 * a name was written. */
static bool emit_type_id(buf_t *b, const zsp_ast_node_t *decl,
                         const zsp_source_t *src)
{
    if (decl == NULL) return false;
    const zsp_ast_node_t *name_node = NULL;
    switch (decl->kind) {
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:        name_node = decl->u.compound.name;       break;
    case ZSP_AST_CHOICE:       name_node = decl->u.choice.name;         break;
    case ZSP_AST_ENUM:         name_node = decl->u.enum_decl.name;      break;
    case ZSP_AST_BITMASK:      name_node = decl->u.bitmask_decl.name;   break;
    case ZSP_AST_SUBTYPE:      name_node = decl->u.subtype.alias_name;  break;
    case ZSP_AST_CONST:        name_node = decl->u.const_decl.name;     break;
    case ZSP_AST_INSTANTIATE:  name_node = decl->u.instantiate.name;    break;
    case ZSP_AST_SQL_TABLE:    name_node = decl->u.sql_table.name;      break;
    case ZSP_AST_SQL_DATABASE: name_node = decl->u.sql_database.name;   break;
    default:                   return false;
    }
    const uint8_t *bytes = NULL;
    size_t         len   = 0U;
    if (!ident_span(src, name_node, &bytes, &len)) return false;
    emit_pkg_prefix(b, decl, src);
    emit_snake(b, bytes, len);
    return true;
}

/* Walk a compound's members and emit `#include "<typename>.h"`
 * for every referenced named type (enum / bitmask / nested
 * compound). Deduplicates by linear search through an
 * arena-allocated string list. */
static void emit_referenced_includes(buf_t *b, const zsp_ast_node_t *cmpd,
                                     const zsp_source_t *src)
{
    /* Small fixed-cap table; most compounds reference << 32 types. */
    enum { MAX_INC = 512U };
    const uint8_t *names[MAX_INC];
    size_t          lens[MAX_INC];
    const zsp_ast_node_t *decls_for_pkg[MAX_INC];  /* for path computation */
    size_t          n = 0U;

    /* Compute the current compound's own package path so we
     * can decide whether each include needs path-qualification. */
    char self_pkg[1024];
    (void)decl_package_path(cmpd, src, self_pkg, sizeof(self_pkg));

    /* Collect referenced const decls used in array length /
     * if-clause / init expressions so we can include their
     * headers. The set is small (typically 0–2 consts per
     * compound) — linear walk is fine. */
    const zsp_ast_node_t *const_decls[32];
    size_t                n_const_decls = 0U;

    /* Collect referenced types from parameters AND fields. */
    const zsp_ast_node_t *all_types[1024];
    size_t                n_types = 0U;
    for (size_t i = 0U; (i < cmpd->u.compound.n_params) && (n_types < 1024U); ++i) {
        all_types[n_types++] = cmpd->u.compound.params[i]->u.parameter.type;
    }
    for (size_t i = 0U; (i < cmpd->u.compound.n_members) && (n_types < 1024U); ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
        all_types[n_types++] = m->u.field.type;
        /* Scan field's array length + init + if-clause + align +
         * offset for IDENT references to const decls. */
        /* Dynamic-width bitfield references its width expr from
         * the type node. Include both that and the array-length
         * expr along with the field-level guards. */
        const zsp_ast_node_t *bf_width_expr = NULL;
        if ((m->u.field.type != NULL)
                && (m->u.field.type->kind == ZSP_AST_TYPE_BUILTIN)) {
            bf_width_expr = m->u.field.type->u.type_builtin.width_expr;
        }
        const zsp_ast_node_t *arr_elem_width_expr = NULL;
        if ((m->u.field.type != NULL)
                && (m->u.field.type->kind == ZSP_AST_TYPE_ARRAY)
                && (m->u.field.type->u.type_array.element != NULL)
                && (m->u.field.type->u.type_array.element->kind == ZSP_AST_TYPE_BUILTIN)) {
            arr_elem_width_expr =
                    m->u.field.type->u.type_array.element->u.type_builtin.width_expr;
        }
        const zsp_ast_node_t *exprs[] = {
            (m->u.field.type && (m->u.field.type->kind == ZSP_AST_TYPE_ARRAY))
                ? m->u.field.type->u.type_array.length_expr : NULL,
            m->u.field.init_expr,
            m->u.field.if_clause,
            m->u.field.constraint_expr,
            m->u.field.align_expr,
            m->u.field.offset_expr,
            bf_width_expr,
            arr_elem_width_expr,
        };
        for (size_t e = 0U; e < (sizeof(exprs) / sizeof(exprs[0])); ++e) {
            /* DFS through the expression tree looking for IDENT
             * references that resolve to CONST decls. The stack
             * is bounded by expression depth which is small for
             * field constraint/if-clause/length exprs. */
            const zsp_ast_node_t *stack[64];
            size_t                sp = 0U;
            if (exprs[e] != NULL) stack[sp++] = exprs[e];
            while ((sp > 0U) && (n_const_decls < 32U)) {
                const zsp_ast_node_t *x = stack[--sp];
                if (x == NULL) continue;
                /* A dotted reference may resolve to a CONST on
                 * its outer MEMBER node (package-qualified const)
                 * or on a bare IDENT (unqualified const). */
                const zsp_ast_node_t *d = x->resolved;
                if ((d != NULL) && (d->kind == ZSP_AST_CONST)) {
                    bool seen = false;
                    for (size_t k = 0U; k < n_const_decls; ++k) {
                        if (const_decls[k] == d) { seen = true; break; }
                    }
                    if (!seen) const_decls[n_const_decls++] = d;
                }
                if (x->kind == ZSP_AST_EXPR_BINARY) {
                    if ((sp + 1U) < 64U) { stack[sp++] = x->u.binop.lhs; stack[sp++] = x->u.binop.rhs; }
                }
                else if (x->kind == ZSP_AST_EXPR_UNARY) {
                    if (sp < 64U) stack[sp++] = x->u.unop.operand;
                }
                else if (x->kind == ZSP_AST_EXPR_TERNARY) {
                    if ((sp + 2U) < 64U) {
                        stack[sp++] = x->u.ternary.cond;
                        stack[sp++] = x->u.ternary.then_expr;
                        stack[sp++] = x->u.ternary.else_expr;
                    }
                }
                else if (x->kind == ZSP_AST_EXPR_MEMBER) {
                    if (sp < 64U) stack[sp++] = x->u.member.lhs;
                }
                else if (x->kind == ZSP_AST_EXPR_INDEX) {
                    if ((sp + 1U) < 64U) {
                        stack[sp++] = x->u.index.lhs;
                        stack[sp++] = x->u.index.index;
                    }
                }
                else if (x->kind == ZSP_AST_EXPR_CALL) {
                    for (size_t a = 0U; (a < x->u.call.n_args) && (sp < 64U); ++a) {
                        stack[sp++] = x->u.call.args[a];
                    }
                }
            }
        }
    }
    for (size_t idx = 0U; idx < n_types; ++idx) {
        const zsp_ast_node_t *ty = all_types[idx];
        if (ty == NULL) continue;
        if (ty->kind == ZSP_AST_TYPE_ARRAY) {
            ty = ty->u.type_array.element;
        }
        if ((ty == NULL) || (ty->kind != ZSP_AST_TYPE_REF)) continue;
        const zsp_ast_node_t *decl = ty->resolved;
        if (decl == NULL) continue;
        const zsp_ast_node_t *name_node = NULL;
        if ((decl->kind == ZSP_AST_STRUCT) || (decl->kind == ZSP_AST_UNION)) {
            name_node = decl->u.compound.name;
        }
        else if (decl->kind == ZSP_AST_CHOICE) {
            name_node = decl->u.choice.name;
        }
        else if (decl->kind == ZSP_AST_ENUM) {
            name_node = decl->u.enum_decl.name;
        }
        else if (decl->kind == ZSP_AST_BITMASK) {
            name_node = decl->u.bitmask_decl.name;
        }
        else if (decl->kind == ZSP_AST_SUBTYPE) {
            name_node = decl->u.subtype.alias_name;
        }
        else if (decl->kind == ZSP_AST_SQL_TABLE) {
            name_node = decl->u.sql_table.name;
        }
        const uint8_t *nb = NULL;
        size_t         nl = 0U;
        if (!ident_span(src, name_node, &nb, &nl)) continue;
        /* Dedup by (name, package) — same simple name from
         * different packages must each contribute a distinct
         * include line. */
        bool seen = false;
        for (size_t k = 0U; k < n; ++k) {
            if ((lens[k] != nl) || (memcmp(names[k], nb, nl) != 0)) continue;
            char a_pkg[1024], b_pkg[1024];
            const size_t la = decl_package_path(decl, src, a_pkg, sizeof(a_pkg));
            const size_t lb = decl_package_path(decls_for_pkg[k], src, b_pkg, sizeof(b_pkg));
            if (la == lb && memcmp(a_pkg, b_pkg, la) == 0) {
                seen = true;
                break;
            }
        }
        if (seen || (n >= MAX_INC)) continue;
        names[n] = nb;
        lens[n]  = nl;
        decls_for_pkg[n] = decl;
        n += 1U;
        /* Also include any INSTANTIATE aliases (in the compound's
         * own unit) whose target resolves to this same concrete
         * clone. The replace_template_refs pass derefs INSTANTIATE
         * → concrete on the field's type_ref, so the name there
         * is the concrete's mangled name; the harness may still
         * spell the field type via the alias and expects its
         * typedef header to be in scope. */
        const zsp_ast_node_t *self_unit = cmpd->resolved;
        if ((self_unit != NULL) && (self_unit->kind == ZSP_AST_UNIT)) {
            for (size_t u = 0U; u < self_unit->u.unit.n_decls; ++u) {
                const zsp_ast_node_t *id = self_unit->u.unit.decls[u];
                if ((id == NULL) || (id->kind != ZSP_AST_INSTANTIATE)) continue;
                const zsp_ast_node_t *itgt = id->u.instantiate.target;
                if ((itgt == NULL) || (itgt->resolved != decl)) continue;
                const uint8_t *anb = NULL; size_t anl = 0U;
                if (!ident_span(src, id->u.instantiate.name, &anb, &anl)) continue;
                bool aseen = false;
                for (size_t k = 0U; k < n; ++k) {
                    if ((lens[k] == anl) && (memcmp(names[k], anb, anl) == 0)) {
                        aseen = true; break;
                    }
                }
                if (aseen || (n >= MAX_INC)) continue;
                names[n] = anb;
                lens[n]  = anl;
                decls_for_pkg[n] = (const zsp_ast_node_t *)id;
                n += 1U;
            }
        }
    }

    /* Count `..` levels needed to climb from the current
     * compound's directory to the output root. */
    const size_t self_len = strlen(self_pkg);
    size_t levels_up = 0U;
    if (self_len > 0U) {
        levels_up = 1U;   /* the package dir itself */
        for (size_t k = 0U; k < self_len; ++k) {
            if (self_pkg[k] == '/') levels_up += 1U;
        }
    }
    /* Helper inline: emit `#include "..."` with path-prefix when
     * the referenced decl is in a different package than the
     * current compound. */
    for (size_t i = 0U; i < n; ++i) {
        char dep_pkg[1024];
        const size_t dep_pkg_len = decl_package_path(
                decls_for_pkg[i], src, dep_pkg, sizeof(dep_pkg));
        const bool same_pkg =
                ((self_len == dep_pkg_len)
                 && (memcmp(self_pkg, dep_pkg, dep_pkg_len) == 0));
        emit_lit(b, "#include \"");
        if (!same_pkg) {
            for (size_t k = 0U; k < levels_up; ++k) {
                emit_lit(b, "../");
            }
            if (dep_pkg_len > 0U) {
                emit_bytes(b, dep_pkg, dep_pkg_len);
                emit_lit(b, "/");
            }
        }
        emit_snake(b, names[i], lens[i]);
        emit_lit(b, ".h\"\n");
    }
    /* Const-decl includes (same package qualification logic). */
    for (size_t i = 0U; i < n_const_decls; ++i) {
        const zsp_ast_node_t *cn = const_decls[i]->u.const_decl.name;
        const uint8_t *cb = NULL;
        size_t         cl = 0U;
        if (ident_span(src, cn, &cb, &cl)) {
            char dep_pkg[1024];
            const size_t dep_pkg_len = decl_package_path(
                    const_decls[i], src, dep_pkg, sizeof(dep_pkg));
            const bool same_pkg =
                    ((self_len == dep_pkg_len)
                     && (memcmp(self_pkg, dep_pkg, dep_pkg_len) == 0));
            emit_lit(b, "#include \"");
            if (!same_pkg) {
                for (size_t k = 0U; k < levels_up; ++k) {
                    emit_lit(b, "../");
                }
                if (dep_pkg_len > 0U) {
                    emit_bytes(b, dep_pkg, dep_pkg_len);
                    emit_lit(b, "/");
                }
            }
            emit_snake(b, cb, cl);
            emit_lit(b, ".h\"\n");
        }
    }
    if ((n > 0U) || (n_const_decls > 0U)) {
        emit_lit(b, "\n");
    }
}

static void emit_header_for_compound(buf_t *b, const zsp_ast_node_t *cmpd,
                                     const zsp_source_t *src)
{
    const uint8_t *name = NULL;
    size_t         nlen = 0U;
    (void)ident_span(src, cmpd->u.compound.name, &name, &nlen);

    /* Header guard — package-qualified to avoid collisions across
     * different packages sharing a simple type name. */
    emit_lit(b, "/* Auto-generated by zserio-c parser. Do not edit. */\n");
    emit_lit(b, "#ifndef ZSP_GENERATED_");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_H_\n");
    emit_lit(b, "#define ZSP_GENERATED_");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_H_\n\n");

    emit_lit(b, "#include <stdbool.h>\n");
    emit_lit(b, "#include <stddef.h>\n");
    emit_lit(b, "#include <stdint.h>\n");
    emit_lit(b, "#include <string.h>\n\n");
    emit_lit(b, "#include \"zserio/allocator.h\"\n");
    emit_lit(b, "#include \"zserio/bitstream.h\"\n");
    emit_lit(b, "#include \"zserio/bytes.h\"\n");
    emit_lit(b, "#include \"zserio/extern.h\"\n");
    emit_lit(b, "#include \"zserio/float.h\"\n");
    emit_lit(b, "#include \"zserio/primitives.h\"\n");
    emit_lit(b, "#include \"zserio/status.h\"\n");
    emit_lit(b, "#include \"zserio/string.h\"\n");
    emit_lit(b, "#include \"zserio/varint.h\"\n\n");

    emit_referenced_includes(b, cmpd, src);

    /* For templated compounds, emit a placeholder typedef per
     * template parameter so the generated code at least compiles.
     * Proper template instantiation (per-use concrete types)
     * lands in a follow-up slice. */
    for (size_t i = 0U; i < cmpd->u.compound.n_template_params; ++i) {
        const zsp_ast_node_t *tp = cmpd->u.compound.template_params[i];
        const uint8_t *tpn = NULL;
        size_t         tpl = 0U;
        if (ident_span(src, tp->u.template_param.name, &tpn, &tpl)) {
            /* Placeholder typedef so the generated C compiles
             * before full template instantiation lands. int64_t
             * is switch-compatible (used as choice selector) and
             * representation-equivalent to any zserio scalar; for
             * compound template args the generated code may need
             * additional template-instantiation work to be wire-
             * correct, but it compiles. */
            emit_lit(b, "typedef int64_t ");
            emit_snake(b, tpn, tpl);
            emit_lit(b, "_t;  /* template-param placeholder */\n");
        }
    }
    if (cmpd->u.compound.n_template_params > 0U) {
        emit_lit(b, "\n");
    }

    emit_struct_decl(b, cmpd, src);

    /* For unions, also emit `#define <pkg>_<UNIONNAME>_TAG_<ARM>`
     * per arm so harness drivers can set `pkg.tag` symbolically. */
    if (cmpd->kind == ZSP_AST_UNION) {
        const uint8_t *un = NULL;
        size_t         ul = 0U;
        (void)ident_span(src, cmpd->u.compound.name, &un, &ul);
        emit_lit(b, "\n");
        size_t arm = 0U;
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if (m->kind != ZSP_AST_FIELD) continue;
            const uint8_t *fn = NULL;
            size_t         fl = 0U;
            if (ident_span(src, m->u.field.name, &fn, &fl)) {
                emit_lit(b, "#define ");
                emit_pkg_prefix(b, cmpd, src);
                emit_upper_snake(b, un, ul);
                emit_lit(b, "_TAG_");
                emit_upper_snake(b, fn, fl);
                emit_lit(b, " ");
                emit_uint(b, (uint64_t)arm);
                emit_lit(b, "u\n");
            }
            arm++;
        }
        emit_lit(b, "\n");
    }

    emit_packing_ctx_typedef(b, cmpd, src);
    emit_compound_protos(b, cmpd, src);

    emit_lit(b, "\n#endif\n");
}

/* ----- Expression emission for array lengths + similar ------ */

/* Emit a constant or simple-reference expression as C. Supports
 * literals, identifiers that resolved to a field/parameter,
 * member access. Complex expressions emit an "unsupported-expr"
 * marker and a 0 fallback so the surrounding read still
 * compiles. */
static void emit_expr_c(buf_t *b, const zsp_ast_node_t *e,
                        const zsp_source_t *src)
{
    if (e == NULL) {
        emit_lit(b, "0");
        return;
    }
    switch (e->kind) {
    case ZSP_AST_EXPR_INT_LIT:
        emit_uint(b, e->u.int_lit.value);
        return;
    case ZSP_AST_EXPR_BOOL_LIT:
        emit_lit(b, e->u.bool_lit.value ? "true" : "false");
        return;
    case ZSP_AST_EXPR_IDENT: {
        /* Resolved identifier: field/parameter → self->name. */
        const zsp_ast_node_t *const d = e->resolved;
        const zsp_file_t *f = NULL;
        if (!zsp_source_get(src, e->file_id, &f)) {
            emit_lit(b, "0");
            return;
        }
        const uint8_t *const name = &f->bytes[e->u.ident.span_off];
        const size_t         len  = (size_t)e->u.ident.span_len;
        /* `@index` is the zserio pseudo-identifier for the array
         * loop's index. Lower to the loop variable `i_` that the
         * enclosing array read/write emits. */
        if ((len >= 6U) && (name[0] == (uint8_t)'@')
                && (memcmp(name, "@index", 6U) == 0)) {
            emit_lit(b, "i_");
            return;
        }
        if (d != NULL) {
            if ((d->kind == ZSP_AST_FIELD)
                    || (d->kind == ZSP_AST_PARAMETER)) {
                if (b->this_override != NULL) {
                    emit_lit(b, b->this_override);
                    emit_lit(b, ".");
                } else {
                    emit_lit(b, "self->");
                }
                emit_snake(b, name, len);
                return;
            }
            if ((d->kind == ZSP_AST_ENUM_ITEM)
                    || (d->kind == ZSP_AST_BITMASK_ITEM)) {
                /* Emit the computed integer value — works as a
                 * `case` label or expression operand. We don't
                 * have a parent-pointer back to the enum name
                 * on the AST, but const-eval already cached the
                 * resolved value. */
                const zsp_sema_const_t *const v = (const zsp_sema_const_t *)
                        d->computed;
                if (v != NULL) {
                    emit_uint(b, v->v.u);
                    return;
                }
                /* Fallback (shouldn't normally trigger): emit the
                 * bare spelling and hope the user supplies a
                 * matching macro. */
                emit_span(b, name, len);
                return;
            }
            if (d->kind == ZSP_AST_CONST) {
                /* Const decls are emitted as `#define <pkg>_NAME (...)`
                 * preserving the const's original case, so reference
                 * sites emit `<pkg>_NAME` verbatim. */
                emit_pkg_prefix(b, d, src);
                emit_span(b, name, len);
                return;
            }
            if (d->kind == ZSP_AST_TEMPLATE_PARAM) {
                /* Template param in an unbound context — emit a
                 * placeholder integer so case labels and similar
                 * constant-required positions compile. The
                 * generated code is template-instantiation pending. */
                emit_lit(b, "0");
                return;
            }
        }
        /* Fallback: emit as-is (best effort). */
        emit_span(b, name, len);
        return;
    }
    case ZSP_AST_EXPR_BINARY:
        emit_lit(b, "(");
        emit_expr_c(b, e->u.binop.lhs, src);
        switch (e->u.binop.op) {
        case ZSP_OP_ADD: emit_lit(b, " + ");  break;
        case ZSP_OP_SUB: emit_lit(b, " - ");  break;
        case ZSP_OP_MUL: emit_lit(b, " * ");  break;
        case ZSP_OP_DIV: emit_lit(b, " / ");  break;
        case ZSP_OP_MOD: emit_lit(b, " % "); break;
        case ZSP_OP_LSHIFT: emit_lit(b, " << "); break;
        case ZSP_OP_RSHIFT: emit_lit(b, " >> "); break;
        case ZSP_OP_BIT_AND: emit_lit(b, " & "); break;
        case ZSP_OP_BIT_OR:  emit_lit(b, " | "); break;
        case ZSP_OP_BIT_XOR: emit_lit(b, " ^ "); break;
        case ZSP_OP_LOGICAL_AND: emit_lit(b, " && "); break;
        case ZSP_OP_LOGICAL_OR:  emit_lit(b, " || "); break;
        case ZSP_OP_EQ:  emit_lit(b, " == "); break;
        case ZSP_OP_NEQ: emit_lit(b, " != "); break;
        case ZSP_OP_LT:  emit_lit(b, " < ");  break;
        case ZSP_OP_GT:  emit_lit(b, " > ");  break;
        case ZSP_OP_LE:  emit_lit(b, " <= "); break;
        case ZSP_OP_GE:  emit_lit(b, " >= "); break;
        default:         emit_lit(b, " /*?*/ "); break;
        }
        emit_expr_c(b, e->u.binop.rhs, src);
        emit_lit(b, ")");
        return;
    case ZSP_AST_EXPR_UNARY:
        switch (e->u.unop.op) {
        case ZSP_OP_NEG:         emit_lit(b, "-"); break;
        case ZSP_OP_BIT_NOT:     emit_lit(b, "~"); break;
        case ZSP_OP_LOGICAL_NOT: emit_lit(b, "!"); break;
        default:                 emit_lit(b, "/*?*/"); break;
        }
        emit_expr_c(b, e->u.unop.operand, src);
        return;
    case ZSP_AST_EXPR_MEMBER:
        /* `LHS.RHS`: if RHS resolved to an enum/bitmask item with
         * a computed value, emit that value directly. */
        if (e->resolved != NULL) {
            if ((e->resolved->kind == ZSP_AST_ENUM_ITEM)
                    || (e->resolved->kind == ZSP_AST_BITMASK_ITEM)) {
                const zsp_sema_const_t *const v = (const zsp_sema_const_t *)
                        e->resolved->computed;
                if (v != NULL) {
                    emit_uint(b, v->v.u);
                    return;
                }
            }
            /* `pkg.NAME` resolving to a top-level CONST — emit the
             * package-qualified macro name; the const-decl scan
             * pulled in the matching #include earlier. */
            if (e->resolved->kind == ZSP_AST_CONST) {
                const zsp_ast_node_t *cname = e->u.member.name;
                const uint8_t *cb = NULL;
                size_t         cl = 0U;
                if (ident_span(src, cname, &cb, &cl)) {
                    emit_pkg_prefix(b, e->resolved, src);
                    emit_span(b, cb, cl);
                    return;
                }
            }
        }
        /* Post-template-clone: LHS may have been retargeted to
         * the concrete ENUM/BITMASK decl (via the substituted
         * type-arg). Look up the named item ourselves and emit
         * its computed value. */
        if ((e->u.member.lhs != NULL)
                && (e->u.member.lhs->resolved != NULL)
                && (e->u.member.name != NULL)) {
            zsp_ast_node_t *base = e->u.member.lhs->resolved;
            while ((base != NULL) && (base->kind == ZSP_AST_SUBTYPE)
                    && (base->u.subtype.target_type != NULL)) {
                base = base->u.subtype.target_type->resolved;
            }
            const uint8_t *needle = NULL;
            size_t         need_len = 0U;
            if (ident_span(src, e->u.member.name, &needle, &need_len)
                    && (base != NULL)) {
                if (base->kind == ZSP_AST_ENUM) {
                    for (size_t i = 0U; i < base->u.enum_decl.n_items; ++i) {
                        const zsp_ast_node_t *it = base->u.enum_decl.items[i];
                        const uint8_t *ib = NULL; size_t il = 0U;
                        if (ident_span(src, it->u.enum_item.name, &ib, &il)
                                && (il == need_len)
                                && (memcmp(ib, needle, il) == 0)) {
                            const zsp_sema_const_t *v = (const zsp_sema_const_t *)it->computed;
                            if (v != NULL) { emit_uint(b, v->v.u); return; }
                        }
                    }
                }
                else if (base->kind == ZSP_AST_BITMASK) {
                    for (size_t i = 0U; i < base->u.bitmask_decl.n_items; ++i) {
                        const zsp_ast_node_t *it = base->u.bitmask_decl.items[i];
                        const uint8_t *ib = NULL; size_t il = 0U;
                        if (ident_span(src, it->u.bitmask_item.name, &ib, &il)
                                && (il == need_len)
                                && (memcmp(ib, needle, il) == 0)) {
                            const zsp_sema_const_t *v = (const zsp_sema_const_t *)it->computed;
                            if (v != NULL) { emit_uint(b, v->v.u); return; }
                        }
                    }
                }
            }
        }
        /* Unresolved member access (e.g. on a template parameter):
         * emit 0 as a placeholder so the surrounding case label
         * or constant expression compiles. */
        if ((e->u.member.lhs != NULL)
                && (e->u.member.lhs->kind == ZSP_AST_EXPR_IDENT)
                && (e->u.member.lhs->resolved != NULL)
                && (e->u.member.lhs->resolved->kind == ZSP_AST_TEMPLATE_PARAM)) {
            emit_lit(b, "0");
            return;
        }
        if (e->u.member.lhs != NULL) {
            emit_expr_c(b, e->u.member.lhs, src);
            emit_lit(b, ".");
        }
        if (e->u.member.name != NULL) {
            const zsp_file_t *f = NULL;
            if (zsp_source_get(src, e->u.member.name->file_id, &f)) {
                const uint8_t *n = &f->bytes[e->u.member.name->u.ident.span_off];
                const size_t   nl = (size_t)e->u.member.name->u.ident.span_len;
                emit_snake(b, n, nl);
            }
        }
        return;
    case ZSP_AST_EXPR_INDEX:
        /* `arr[idx]` — array indexing in a constraint/if-clause. */
        if (e->u.index.lhs != NULL) {
            emit_expr_c(b, e->u.index.lhs, src);
        }
        emit_lit(b, "[");
        if (e->u.index.index != NULL) {
            emit_expr_c(b, e->u.index.index, src);
        }
        emit_lit(b, "]");
        return;
    case ZSP_AST_EXPR_TERNARY:
        emit_lit(b, "(");
        if (e->u.ternary.cond != NULL) emit_expr_c(b, e->u.ternary.cond, src);
        emit_lit(b, " ? ");
        if (e->u.ternary.then_expr != NULL) emit_expr_c(b, e->u.ternary.then_expr, src);
        emit_lit(b, " : ");
        if (e->u.ternary.else_expr != NULL) emit_expr_c(b, e->u.ternary.else_expr, src);
        emit_lit(b, ")");
        return;
    case ZSP_AST_EXPR_CALL: {
        /* `isset(bitmask, flag)` is the only schema-level builtin
         * we lower. Schema-defined functions (zero-arg only) are
         * inlined by emitting their body expression in place. */
        const zsp_ast_node_t *callee = e->u.call.callee;
        if (callee && (callee->kind == ZSP_AST_EXPR_IDENT)) {
            const zsp_file_t *f = NULL;
            if (zsp_source_get(src, callee->file_id, &f)) {
                const uint8_t *n = &f->bytes[callee->u.ident.span_off];
                const size_t   nl = (size_t)callee->u.ident.span_len;
                if ((nl == 5U) && (memcmp(n, "isset", 5U) == 0)
                        && (e->u.call.n_args == 2U)) {
                    emit_lit(b, "(((");
                    emit_expr_c(b, e->u.call.args[0], src);
                    emit_lit(b, ") & (");
                    emit_expr_c(b, e->u.call.args[1], src);
                    emit_lit(b, ")) == (");
                    emit_expr_c(b, e->u.call.args[1], src);
                    emit_lit(b, "))");
                    return;
                }
                /* `valueof(enumItem)` — pass through. Our IDENT
                 * emission for an EXPR_MEMBER pointing at an
                 * enum/bitmask item already emits the computed
                 * integer value. */
                if ((nl == 7U) && (memcmp(n, "valueof", 7U) == 0)
                        && (e->u.call.n_args == 1U)) {
                    emit_lit(b, "(");
                    emit_expr_c(b, e->u.call.args[0], src);
                    emit_lit(b, ")");
                    return;
                }
                /* `numbits(x)` — bits needed to encode `x+1`
                 * distinct values = ceil(log2(x+1)). Inline
                 * with `__builtin_clzll`. */
                if ((nl == 7U) && (memcmp(n, "numbits", 7U) == 0)
                        && (e->u.call.n_args == 1U)) {
                    emit_lit(b, "((uint64_t)(");
                    emit_expr_c(b, e->u.call.args[0], src);
                    emit_lit(b, ") <= 1u ? 1u : (uint64_t)(64u - __builtin_clzll((uint64_t)(");
                    emit_expr_c(b, e->u.call.args[0], src);
                    emit_lit(b, ") - 1u)))");
                    return;
                }
                /* `lengthof(arr)` — array element count. With our
                 * struct layout, dynamic arrays carry a sibling
                 * `_count`; fixed-size arrays evaluate to their
                 * const length at compile time. */
                if ((nl == 8U) && (memcmp(n, "lengthof", 8U) == 0)
                        && (e->u.call.n_args == 1U)) {
                    const zsp_ast_node_t *const a = e->u.call.args[0];
                    if ((a != NULL) && (a->kind == ZSP_AST_EXPR_IDENT)
                            && (a->resolved != NULL)
                            && (a->resolved->kind == ZSP_AST_FIELD)) {
                        const zsp_ast_node_t *fty = a->resolved->u.field.type;
                        const zsp_ast_node_t *lenx = (fty != NULL
                                && (fty->kind == ZSP_AST_TYPE_ARRAY))
                                ? fty->u.type_array.length_expr : NULL;
                        const zsp_sema_const_t *lenc = (lenx != NULL)
                                ? (const zsp_sema_const_t *)lenx->computed
                                : NULL;
                        if ((lenc != NULL)
                                && (lenc->kind == ZSP_CONST_UINT)
                                && (lenc->v.u > 0U)) {
                            emit_uint(b, lenc->v.u);
                            return;
                        }
                        const zsp_file_t *af = NULL;
                        if (zsp_source_get(src, a->file_id, &af)) {
                            const uint8_t *an = &af->bytes[a->u.ident.span_off];
                            const size_t   al = (size_t)a->u.ident.span_len;
                            if (b->this_override != NULL) {
                                emit_lit(b, b->this_override);
                                emit_lit(b, ".");
                            } else {
                                emit_lit(b, "self->");
                            }
                            emit_snake(b, an, al);
                            emit_lit(b, "_count");
                            return;
                        }
                    }
                    emit_lit(b, "0");
                    return;
                }
            }
            /* Schema-defined function reference: inline the body
             * expression. The function lives on the same compound
             * and references field/parameter values via
             * `self->...`, which the caller already has in scope. */
            if ((callee->resolved != NULL)
                    && (callee->resolved->kind == ZSP_AST_FUNCTION)
                    && (callee->resolved->u.function_decl.body_expr != NULL)) {
                emit_lit(b, "(");
                emit_expr_c(b, callee->resolved->u.function_decl.body_expr, src);
                emit_lit(b, ")");
                return;
            }
        }
        /* Member-function call: `<recv>.<func>()` where <func>
         * resolves to a schema FUNCTION on the type of <recv>.
         * Inline the body, but with `this` pointing at <recv>
         * — IDENT references to FIELD/PARAMETER in the body
         * become `<recv-emission>.<name>` instead of the
         * default `self-><name>`. */
        if ((callee != NULL) && (callee->kind == ZSP_AST_EXPR_MEMBER)
                && (callee->resolved != NULL)
                && (callee->resolved->kind == ZSP_AST_FUNCTION)
                && (callee->resolved->u.function_decl.body_expr != NULL)
                && (callee->u.member.lhs != NULL)) {
            /* Render the receiver into a small scratch buffer
             * so we can use it as a literal prefix. */
            buf_t recv_buf;
            buf_init(&recv_buf, b->arena);
            recv_buf.this_override = b->this_override;
            emit_expr_c(&recv_buf, callee->u.member.lhs, src);
            /* Strip a trailing arrow/dot so we can append `.field`. */
            if ((recv_buf.len > 0U) && !recv_buf.oom) {
                char *recv_str = (char *)zsp_arena_alloc(
                        b->arena, recv_buf.len + 1U, 1U);
                if (recv_str != NULL) {
                    (void)memcpy(recv_str, recv_buf.bytes, recv_buf.len);
                    recv_str[recv_buf.len] = '\0';
                    const char *old = b->this_override;
                    b->this_override = recv_str;
                    emit_lit(b, "(");
                    emit_expr_c(b, callee->resolved->u.function_decl.body_expr, src);
                    emit_lit(b, ")");
                    b->this_override = old;
                    return;
                }
            }
        }
        emit_lit(b, "0 /*unsupported-call*/");
        return;
    }
    default:
        emit_lit(b, "0 /*unsupported-expr*/");
        return;
    }
}

/* Emit the C type name to use for a field's storage. For
 * arrays the element type is emitted (the caller adds the
 * pointer). For named types we emit `<lowered>_t`. */
static void emit_element_type(buf_t *b, const zsp_ast_node_t *element_ty,
                              const zsp_source_t *src)
{
    if (element_ty == NULL) {
        emit_lit(b, "void");
        return;
    }
    if (element_ty->kind == ZSP_AST_TYPE_BUILTIN) {
        const zsp_builtin_code_t c = element_ty->u.type_builtin.code;
        if ((c == ZSP_TB_BIT_FIELD) || (c == ZSP_TB_INT_FIELD)) {
            const uint32_t w = element_ty->u.type_builtin.width;
            const bool is_signed = (c == ZSP_TB_INT_FIELD);
            if (w > 0U && w <= 8U)        emit_lit(b, is_signed ? "int8_t"  : "uint8_t");
            else if (w > 0U && w <= 16U)  emit_lit(b, is_signed ? "int16_t" : "uint16_t");
            else if (w > 0U && w <= 32U)  emit_lit(b, is_signed ? "int32_t" : "uint32_t");
            else                          emit_lit(b, is_signed ? "int64_t" : "uint64_t");
            return;
        }
        emit_lit(b, builtin_c_type(c));
        return;
    }
    if (element_ty->kind == ZSP_AST_TYPE_REF) {
        const zsp_ast_node_t *decl = element_ty->resolved;
        if ((decl != NULL) && emit_type_id(b, decl, src)) {
            emit_lit(b, "_t");
            return;
        }
        const zsp_ast_node_t *name = element_ty->u.type_ref.name;
        const zsp_ast_node_t *id = (name && name->kind == ZSP_AST_DOTTED_NAME)
                ? name->u.dotted.parts[name->u.dotted.n_parts - 1U]
                : name;
        const uint8_t *bytes = NULL;
        size_t         len   = 0U;
        if (ident_span(src, id, &bytes, &len)) {
            emit_snake(b, bytes, len);
            emit_lit(b, "_t");
            return;
        }
    }
    emit_lit(b, "void");
}

/* For a TYPE_REF, determine if it resolves to an enum/bitmask
 * (which we read via an underlying integer type) and return the
 * underlying builtin code; ZSP_TB_UNKNOWN otherwise. */
static zsp_builtin_code_t enum_underlying(const zsp_ast_node_t *type_ref)
{
    if ((type_ref == NULL) || (type_ref->kind != ZSP_AST_TYPE_REF)) {
        return ZSP_TB_UNKNOWN;
    }
    const zsp_ast_node_t *const decl = peel_alias(type_ref->resolved);
    if (decl == NULL) {
        return ZSP_TB_UNKNOWN;
    }
    const zsp_ast_node_t *base = NULL;
    if (decl->kind == ZSP_AST_ENUM) {
        base = decl->u.enum_decl.base_type;
    }
    else if (decl->kind == ZSP_AST_BITMASK) {
        base = decl->u.bitmask_decl.base_type;
    }
    if ((base != NULL) && (base->kind == ZSP_AST_TYPE_BUILTIN)) {
        return base->u.type_builtin.code;
    }
    return ZSP_TB_UNKNOWN;
}

/* Underlying bit-width for a bit-field-based enum/bitmask base
 * type. 0 if not a bit-field. */
static uint32_t enum_underlying_width(const zsp_ast_node_t *type_ref)
{
    if ((type_ref == NULL) || (type_ref->kind != ZSP_AST_TYPE_REF)) {
        return 0U;
    }
    const zsp_ast_node_t *const decl = peel_alias(type_ref->resolved);
    if (decl == NULL) return 0U;
    const zsp_ast_node_t *base = NULL;
    if (decl->kind == ZSP_AST_ENUM)    base = decl->u.enum_decl.base_type;
    else if (decl->kind == ZSP_AST_BITMASK) base = decl->u.bitmask_decl.base_type;
    if ((base != NULL) && (base->kind == ZSP_AST_TYPE_BUILTIN)) {
        return base->u.type_builtin.width;
    }
    return 0U;
}

/* Resolve element type to the fixed-width-integer parameters
 * needed for a packed array (`zserio_delta_init`):
 *   - num_bits literal (0 if width is dynamic, in which case
 *     `width_expr_out` is set to the AST expression)
 *   - is_signed
 *   - cast_type (C type name to convert delta value <-> slot)
 * Returns true if the element type is supported by the simple
 * fixed-width packed protocol. Returns false for var-int, bool,
 * float, string, extern, bytes, or compound-element packed —
 * caller falls back to unpacked. */
/* packed_elem struct body lives near its forward declaration. */

static bool packed_elem_describe(const zsp_ast_node_t *ty, packed_elem_t *out)
{
    if (ty == NULL) return false;
    /* Peel SUBTYPE aliases. */
    while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
            && (ty->resolved != NULL)
            && (ty->resolved->kind == ZSP_AST_SUBTYPE)
            && (ty->resolved->u.subtype.target_type != NULL)) {
        ty = ty->resolved->u.subtype.target_type;
    }
    out->num_bits = 0U;
    out->width_expr = NULL;
    out->is_signed = false;
    out->cast_type = "uint64_t";
    if (ty->kind == ZSP_AST_TYPE_BUILTIN) {
        const zsp_builtin_code_t c = ty->u.type_builtin.code;
        switch (c) {
        case ZSP_TB_UINT8:  out->num_bits=8;  out->is_signed=false; out->cast_type="uint8_t";  return true;
        case ZSP_TB_UINT16: out->num_bits=16; out->is_signed=false; out->cast_type="uint16_t"; return true;
        case ZSP_TB_UINT32: out->num_bits=32; out->is_signed=false; out->cast_type="uint32_t"; return true;
        case ZSP_TB_UINT64: out->num_bits=64; out->is_signed=false; out->cast_type="uint64_t"; return true;
        case ZSP_TB_INT8:   out->num_bits=8;  out->is_signed=true;  out->cast_type="int8_t";   return true;
        case ZSP_TB_INT16:  out->num_bits=16; out->is_signed=true;  out->cast_type="int16_t";  return true;
        case ZSP_TB_INT32:  out->num_bits=32; out->is_signed=true;  out->cast_type="int32_t";  return true;
        case ZSP_TB_INT64:  out->num_bits=64; out->is_signed=true;  out->cast_type="int64_t";  return true;
        case ZSP_TB_BIT_FIELD:
            if (ty->u.type_builtin.width > 0U) {
                out->num_bits = ty->u.type_builtin.width;
            } else {
                out->width_expr = ty->u.type_builtin.width_expr;
            }
            out->is_signed = false;
            out->cast_type = "uint64_t";
            return true;
        case ZSP_TB_INT_FIELD:
            if (ty->u.type_builtin.width > 0U) {
                out->num_bits = ty->u.type_builtin.width;
            } else {
                out->width_expr = ty->u.type_builtin.width_expr;
            }
            out->is_signed = true;
            out->cast_type = "int64_t";
            return true;
        default: return false;
        }
    }
    if (ty->kind == ZSP_AST_TYPE_REF) {
        const zsp_builtin_code_t c = enum_underlying(ty);
        const uint32_t           w = enum_underlying_width(ty);
        switch (c) {
        case ZSP_TB_UINT8:  out->num_bits=8;  out->is_signed=false; break;
        case ZSP_TB_UINT16: out->num_bits=16; out->is_signed=false; break;
        case ZSP_TB_UINT32: out->num_bits=32; out->is_signed=false; break;
        case ZSP_TB_UINT64: out->num_bits=64; out->is_signed=false; break;
        case ZSP_TB_INT8:   out->num_bits=8;  out->is_signed=true;  break;
        case ZSP_TB_INT16:  out->num_bits=16; out->is_signed=true;  break;
        case ZSP_TB_INT32:  out->num_bits=32; out->is_signed=true;  break;
        case ZSP_TB_INT64:  out->num_bits=64; out->is_signed=true;  break;
        case ZSP_TB_BIT_FIELD: out->num_bits = w; out->is_signed=false; break;
        case ZSP_TB_INT_FIELD: out->num_bits = w; out->is_signed=true;  break;
        default: return false;
        }
        /* Cast to the resolved enum's C type so the slot
         * assignment compiles. */
        out->cast_type = NULL; /* sentinel: emit element_type via emit_element_type */
        return true;
    }
    return false;
}

/* Decide which read function + signature shape to use for a
 * single element. Returns a token: 'p' for primitive call
 * (`fn(bs, &out)`), 'o' for owned call (`fn(bs, alloc, &out)`),
 * 'c' for nested compound (`<T>_read(&out, bs, alloc)`),
 * 'e' for enum/bitmask (read underlying integer then cast),
 * '?' for unsupported (skips emission). */
static char element_read_shape(const zsp_ast_node_t *ty)
{
    if (ty == NULL) return '?';
    if (ty->kind == ZSP_AST_TYPE_BUILTIN) {
        const zsp_builtin_code_t c = ty->u.type_builtin.code;
        return builtin_needs_free(c) ? 'o' : 'p';
    }
    if (ty->kind == ZSP_AST_TYPE_REF) {
        const zsp_ast_node_t *decl = ty->resolved;
        /* Peel SUBTYPE aliases so the underlying shape drives
         * element emission. */
        while ((decl != NULL) && (decl->kind == ZSP_AST_SUBTYPE)
                && (decl->u.subtype.target_type != NULL)
                && (decl->u.subtype.target_type->resolved != NULL)) {
            decl = decl->u.subtype.target_type->resolved;
        }
        if (decl != NULL) {
            if ((decl->kind == ZSP_AST_STRUCT)
                    || (decl->kind == ZSP_AST_UNION)
                    || (decl->kind == ZSP_AST_CHOICE)) {
                return 'c';
            }
            if ((decl->kind == ZSP_AST_ENUM) || (decl->kind == ZSP_AST_BITMASK)) {
                return 'e';
            }
        }
        return '?';
    }
    return '?';
}

/* Emit the per-element read for a single element accessed via
 * `slot_expr`. `slot_expr` is something like "self->name" or
 * "self->name[i]". */
static void emit_element_read(buf_t *b, const zsp_ast_node_t *ty,
                              const char *slot_expr,
                              const zsp_source_t *src)
{
    /* Capture any type-args supplied at the call-site before
     * peeling SUBTYPE aliases — subtype targets drop them. */
    const zsp_ast_node_t *type_args_origin = ty;
    /* Peel subtype aliases so the read uses the underlying type's
     * read function, not the alias name. */
    while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
            && (ty->resolved != NULL)
            && (ty->resolved->kind == ZSP_AST_SUBTYPE)
            && (ty->resolved->u.subtype.target_type != NULL)) {
        ty = ty->resolved->u.subtype.target_type;
    }
    const char shape = element_read_shape(ty);
    /* Bit-field element (explicit or dynamic width). Signed
     * (`int:N` / `int<expr>`) goes through the runtime's
     * sign-extending helper. */
    if ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_BUILTIN)
            && ((ty->u.type_builtin.code == ZSP_TB_BIT_FIELD)
                || (ty->u.type_builtin.code == ZSP_TB_INT_FIELD))) {
        const bool is_signed = (ty->u.type_builtin.code == ZSP_TB_INT_FIELD);
        emit_lit(b, "        {\n");
        emit_lit(b, is_signed ? "            int64_t tmp_;\n"
                              : "            uint64_t tmp_;\n");
        emit_lit(b, is_signed
                ? "            ZSERIO_TRY(zserio_bitstream_read_signed(bs, "
                : "            ZSERIO_TRY(zserio_bitstream_read_bits(bs, ");
        if (ty->u.type_builtin.width > 0U) {
            emit_uint(b, ty->u.type_builtin.width);
        }
        else if (ty->u.type_builtin.width_expr != NULL) {
            emit_lit(b, "(size_t)(");
            emit_expr_c(b, ty->u.type_builtin.width_expr, src);
            emit_lit(b, ")");
        }
        else {
            emit_lit(b, "64");
        }
        emit_lit(b, ", &tmp_));\n");
        emit_lit(b, "            ");
        emit_lit(b, slot_expr);
        emit_lit(b, " = (");
        emit_element_type(b, ty, src);
        emit_lit(b, ")tmp_;\n");
        emit_lit(b, "        }\n");
        return;
    }
    if (shape == 'p') {
        emit_lit(b, "        ZSERIO_TRY(");
        emit_lit(b, builtin_read_fn(ty->u.type_builtin.code));
        emit_lit(b, "(bs, &");
        emit_lit(b, slot_expr);
        emit_lit(b, "));\n");
    }
    else if (shape == 'o') {
        emit_lit(b, "        ZSERIO_TRY(");
        emit_lit(b, builtin_read_fn(ty->u.type_builtin.code));
        emit_lit(b, "(bs, alloc, &");
        emit_lit(b, slot_expr);
        emit_lit(b, "));\n");
    }
    else if (shape == 'c') {
        const zsp_ast_node_t *const decl = peel_alias(ty->resolved);
        emit_lit(b, "        ");
        (void)emit_type_id(b, decl, src);
        emit_lit(b, "_init(&");
        emit_lit(b, slot_expr);
        emit_lit(b, ");\n");
        /* Use the pre-peel type-args when the alias dropped them. */
        const zsp_ast_node_t *args_src = ty;
        if ((args_src == NULL) || (args_src->kind != ZSP_AST_TYPE_REF)
                || (args_src->u.type_ref.n_type_args == 0U)) {
            args_src = type_args_origin;
        }
        if ((args_src != NULL) && (args_src->kind == ZSP_AST_TYPE_REF)
                && (args_src->u.type_ref.n_type_args > 0U)) {
            emit_lit(b, "        ");
            (void)emit_type_id(b, decl, src);
            emit_lit(b, "_set_parameters(&");
            emit_lit(b, slot_expr);
            for (size_t i = 0U; i < args_src->u.type_ref.n_type_args; ++i) {
                emit_lit(b, ", ");
                emit_expr_c(b, args_src->u.type_ref.type_args[i], src);
            }
            emit_lit(b, ");\n");
        }
        emit_lit(b, "        ZSERIO_TRY(");
        (void)emit_type_id(b, decl, src);
        emit_lit(b, "_read(&");
        emit_lit(b, slot_expr);
        emit_lit(b, ", bs, alloc));\n");
    }
    else if (shape == 'e') {
        const zsp_builtin_code_t uc = enum_underlying(ty);
        const uint32_t           uw = enum_underlying_width(ty);
        emit_lit(b, "        {\n");
        if ((uc == ZSP_TB_BIT_FIELD) || (uc == ZSP_TB_INT_FIELD)) {
            const uint32_t w = (uw > 0U) ? uw : 64U;
            emit_lit(b, "            uint64_t tmp_;\n");
            emit_lit(b, "            ZSERIO_TRY(zserio_bitstream_read_bits(bs, ");
            emit_uint(b, w);
            emit_lit(b, ", &tmp_));\n");
        }
        else {
            emit_lit(b, "            ");
            emit_lit(b, builtin_c_type(uc));
            emit_lit(b, " tmp_;\n");
            emit_lit(b, "            ZSERIO_TRY(");
            emit_lit(b, builtin_read_fn(uc));
            emit_lit(b, "(bs, &tmp_));\n");
        }
        emit_lit(b, "            ");
        emit_lit(b, slot_expr);
        emit_lit(b, " = (");
        emit_element_type(b, ty, src);
        emit_lit(b, ")tmp_;\n");
        emit_lit(b, "        }\n");
    }
}

/* Emit the read function body for one field. */
static void emit_field_read(buf_t *b, const zsp_ast_node_t *field,
                            const zsp_source_t *src)
{
    const uint8_t *fn = NULL;
    size_t         fl = 0U;
    if (!ident_span(src, field->u.field.name, &fn, &fl)) {
        return;
    }
    const zsp_ast_node_t *ty = field->u.field.type;
    if (ty == NULL) {
        return;
    }
    const bool is_array = (ty->kind == ZSP_AST_TYPE_ARRAY);
    const zsp_ast_node_t *element_ty = is_array ? ty->u.type_array.element : ty;

    /* When the field is gated by an `if`-clause or is auto-
     * optional, the alignment + offset directives only apply
     * when the payload is actually written. We emit them inside
     * the `if (..._present)` body further below. */
    const bool has_if = (field->u.field.if_clause != NULL)
            || field->u.field.optional;
    if (!has_if) {
        if (field->u.field.align_expr != NULL) {
            emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_reader_align(bs, ");
            emit_expr_c(b, field->u.field.align_expr, src);
            emit_lit(b, "));\n");
        }
        if (field->u.field.offset_expr != NULL) {
            emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_reader_align(bs, 8u));\n");
        }
    }

    if (is_array) {
        /* Array path. Length: explicit expression or auto
         * (varsize length-prefix when length_expr is NULL).
         * Fixed-size arrays (compile-time-constant length) are
         * stored as `T name[N]` inline and read in place — no
         * length prefix, no allocation.
         * Optional (`optional T x[];`) and if-guarded arrays
         * follow the same `_present` flag pattern as scalar
         * fields. */
        const zsp_ast_node_t *const lenexpr = ty->u.type_array.length_expr;
        const zsp_sema_const_t *const lenc = (lenexpr != NULL)
                ? (const zsp_sema_const_t *)lenexpr->computed : NULL;
        const bool fixed = (lenc != NULL) && (lenc->kind == ZSP_CONST_UINT)
                && (lenc->v.u > 0U);
        const zsp_ast_node_t *const if_clause = field->u.field.if_clause;
        const bool has_present =
                field->u.field.optional || (if_clause != NULL);
        if (field->u.field.optional && (if_clause == NULL)) {
            emit_lit(b, "    {\n        uint64_t opt_tmp_; ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1u, &opt_tmp_));\n");
            emit_lit(b, "        self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_present = (opt_tmp_ != 0u);\n    }\n");
            emit_lit(b, "    if (self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_present)\n");
        }
        else if (if_clause != NULL) {
            emit_lit(b, "    self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_present = (");
            emit_expr_c(b, if_clause, src);
            emit_lit(b, ");\n");
            emit_lit(b, "    if (self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_present)\n");
        }
        (void)has_present;

        /* Build the slot expression `self->name[i_]`. */
        char slot[256];
        size_t pos = 0U;
        const char *prefix = "self->";
        const size_t plen = strlen(prefix);
        (void)memcpy(slot, prefix, plen); pos = plen;
        pos = snake_into(slot, pos, fn, fl);
        const char *suffix = "[i_]";
        (void)memcpy(&slot[pos], suffix, strlen(suffix));
        pos += strlen(suffix);
        slot[pos] = '\0';

        emit_lit(b, "    {\n");
        emit_lit(b, "        size_t arr_count_ = 0u;\n");
        if (lenexpr != NULL) {
            emit_lit(b, "        arr_count_ = (size_t)(");
            emit_expr_c(b, lenexpr, src);
            emit_lit(b, ");\n");
        }
        else {
            emit_lit(b, "        uint32_t arr_len_ = 0u;\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_read_varsize(bs, &arr_len_));\n");
            emit_lit(b, "        arr_count_ = (size_t)arr_len_;\n");
        }
        if (!fixed) {
            emit_lit(b, "        self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_count = arr_count_;\n");
            emit_lit(b, "        self->");
            emit_snake(b, fn, fl);
            emit_lit(b, " = NULL;\n");
            emit_lit(b, "        if (arr_count_ > 0u) {\n");
            emit_lit(b, "            self->");
            emit_snake(b, fn, fl);
            emit_lit(b, " = (");
            emit_element_type(b, element_ty, src);
            emit_lit(b, " *)alloc->vtable->alloc(alloc->userdata,\n");
            emit_lit(b, "                    arr_count_ * sizeof(*self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "), sizeof(*self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "));\n");
            emit_lit(b, "            if (self->");
            emit_snake(b, fn, fl);
            emit_lit(b, " == NULL) return ZSERIO_ERR_OUT_OF_MEMORY;\n");
            emit_lit(b, "            (void)memset(self->");
            emit_snake(b, fn, fl);
            emit_lit(b, ", 0, arr_count_ * sizeof(*self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "));\n");
        }
        /* Packed-array fast path: a fixed-width-integer element
         * (incl. bit:N / enums-of-int) uses the runtime's delta
         * context. A varint element uses the runtime's traits-
         * driven delta context. A compound element with at least
         * one packable scalar inside uses per-field-context
         * emission via `<T>_init_packing_context` +
         * `<T>_read_packed`. Other elements fall back to the
         * unpacked per-element read. */
        packed_elem_t pe = {0};
        const bool is_packed = ty->u.type_array.packed
                && packed_elem_describe(element_ty, &pe);
        const char *varint_traits = NULL;
        bool varint_signed = false;
        if (ty->u.type_array.packed && !is_packed
                && (element_ty != NULL)
                && (element_ty->kind == ZSP_AST_TYPE_BUILTIN)) {
            varint_traits = varint_traits_name(
                    element_ty->u.type_builtin.code, &varint_signed);
        }
        /* Detect packed-compound case. */
        const zsp_ast_node_t *cmpd_decl = NULL;
        if (ty->u.type_array.packed && !is_packed
                && (varint_traits == NULL)
                && (element_ty != NULL)
                && (element_ty->kind == ZSP_AST_TYPE_REF)) {
            const zsp_ast_node_t *d = peel_alias(element_ty->resolved);
            if ((d != NULL)
                    && ((d->kind == ZSP_AST_STRUCT) || (d->kind == ZSP_AST_UNION))
                    && compound_has_packable_field(d)) {
                cmpd_decl = d;
            }
        }
        if (is_packed) {
            emit_lit(b, "        zserio_delta_context_t pctx_;\n");
            emit_lit(b, "        (void)memset(&pctx_, 0, sizeof(pctx_));\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_delta_init(&pctx_, ");
            if (pe.num_bits > 0U) {
                emit_uint(b, pe.num_bits);
                emit_lit(b, "u");
            } else if (pe.width_expr != NULL) {
                emit_lit(b, "(uint8_t)(");
                emit_expr_c(b, pe.width_expr, src);
                emit_lit(b, ")");
            } else {
                emit_lit(b, "64u");
            }
            emit_lit(b, ", ");
            emit_lit(b, pe.is_signed ? "true" : "false");
            emit_lit(b, "));\n");
        } else if (varint_traits != NULL) {
            emit_lit(b, "        zserio_delta_context_t pctx_;\n");
            emit_lit(b, "        (void)memset(&pctx_, 0, sizeof(pctx_));\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_delta_init_traits(&pctx_, &");
            emit_lit(b, varint_traits);
            emit_lit(b, "));\n");
        } else if (cmpd_decl != NULL) {
            emit_lit(b, "        ");
            (void)emit_type_id(b, cmpd_decl, src);
            emit_lit(b, "_packing_context_t pctx_;\n");
            emit_lit(b, "        ");
            (void)emit_type_id(b, cmpd_decl, src);
            emit_lit(b, "_init_packing_context(&pctx_);\n");
        }
        emit_lit(b, "        for (size_t i_ = 0u; i_ < arr_count_; ++i_) {\n");
        /* Indexed offsets: byte-align before each element so the
         * reader matches the writer's back-patched byte offsets. */
        if ((field->u.field.offset_expr != NULL)
                && (field->u.field.offset_expr->kind == ZSP_AST_EXPR_INDEX)) {
            emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_reader_align(bs, 8u));\n");
        }
        if (is_packed) {
            emit_lit(b, "        {\n");
            emit_lit(b, pe.is_signed
                    ? "            int64_t pv_;\n            uint64_t pvu_;\n"
                    : "            uint64_t pvu_;\n");
            emit_lit(b, "            ZSERIO_TRY(zserio_delta_read(&pctx_, bs, &pvu_));\n");
            if (pe.is_signed) {
                emit_lit(b, "            pv_ = (int64_t)pvu_;\n            (void)pv_;\n");
            }
            emit_lit(b, "            ");
            emit_lit(b, slot);
            emit_lit(b, " = (");
            if (pe.cast_type != NULL) {
                emit_lit(b, pe.cast_type);
            } else {
                emit_element_type(b, element_ty, src);
            }
            emit_lit(b, ")pvu_;\n");
            emit_lit(b, "        }\n");
        } else if (varint_traits != NULL) {
            emit_lit(b, "        {\n");
            emit_lit(b, "            uint64_t pvu_;\n");
            emit_lit(b, "            ZSERIO_TRY(zserio_delta_read(&pctx_, bs, &pvu_));\n");
            emit_lit(b, "            ");
            emit_lit(b, slot);
            emit_lit(b, " = (");
            emit_element_type(b, element_ty, src);
            emit_lit(b, ")pvu_;\n");
            emit_lit(b, "        }\n");
        } else if (cmpd_decl != NULL) {
            emit_lit(b, "        ");
            (void)emit_type_id(b, cmpd_decl, src);
            emit_lit(b, "_init(&");
            emit_lit(b, slot);
            emit_lit(b, ");\n");
            emit_lit(b, "        ZSERIO_TRY(");
            (void)emit_type_id(b, cmpd_decl, src);
            emit_lit(b, "_read_packed(&");
            emit_lit(b, slot);
            emit_lit(b, ", bs, alloc, &pctx_));\n");
        } else {
            emit_element_read(b, element_ty, slot, src);
        }
        emit_lit(b, "        }\n");
        if (!fixed) {
            emit_lit(b, "        }\n");
        }
        emit_lit(b, "    }\n");
        return;
    }

    /* If-clause wrapping: emit `if (cond)` around the read, and
     * mirror the truthiness into the field's `_present` flag.
     * The field's storage is left zero-initialised (set by init)
     * when the predicate is false, matching upstream semantics.
     * Plain `optional T x;` fields also get a _present flag and
     * read an upfront 1-bit "is set" tag from the wire. */
    const zsp_ast_node_t *const if_clause = field->u.field.if_clause;
    const bool has_present =
            field->u.field.optional || (if_clause != NULL);
    if (field->u.field.optional && (if_clause == NULL)) {
        emit_lit(b, "    {\n        uint64_t opt_tmp_; ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1u, &opt_tmp_));\n");
        emit_lit(b, "        self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_present = (opt_tmp_ != 0u);\n    }\n");
        emit_lit(b, "    if (self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_present) {\n    ");
    }
    else if (if_clause != NULL) {
        emit_lit(b, "    self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_present = (");
        emit_expr_c(b, if_clause, src);
        emit_lit(b, ");\n");
        emit_lit(b, "    if (self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_present) {\n    ");
    }
    /* Inside the conditional body, perform the field's
     * alignment / offset directives so they only fire when the
     * field is actually present (Java semantics). */
    if (has_if) {
        if (field->u.field.align_expr != NULL) {
            emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_reader_align(bs, ");
            emit_expr_c(b, field->u.field.align_expr, src);
            emit_lit(b, "));\n");
        }
        if (field->u.field.offset_expr != NULL) {
            emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_reader_align(bs, 8u));\n");
        }
    }
    (void)has_present;

    /* Scalar field. */
    const char shape = element_read_shape(ty);

    /* Bitfield with explicit OR dynamic width (`bit:N`, `bit<expr>`,
     * `int:N`, `int<expr>`) reads via `zserio_bitstream_read_bits`.
     * For dynamic-width (width==0), the analyser's link to the
     * driving parameter isn't captured on the AST yet — fall back
     * to a 64-bit read of the full storage type so the generated
     * code compiles. Wire-format-equivalent emission for dynamic
     * widths is on the deferred list. */
    if ((ty->kind == ZSP_AST_TYPE_BUILTIN)
            && ((ty->u.type_builtin.code == ZSP_TB_BIT_FIELD)
                || (ty->u.type_builtin.code == ZSP_TB_INT_FIELD))) {
        const bool is_signed = (ty->u.type_builtin.code == ZSP_TB_INT_FIELD);
        emit_lit(b, "    {\n");
        emit_lit(b, is_signed ? "        int64_t tmp_;\n"
                              : "        uint64_t tmp_;\n");
        emit_lit(b, is_signed
                ? "        ZSERIO_TRY(zserio_bitstream_read_signed(bs, "
                : "        ZSERIO_TRY(zserio_bitstream_read_bits(bs, ");
        if (ty->u.type_builtin.width > 0U) {
            emit_uint(b, ty->u.type_builtin.width);
        }
        else if (ty->u.type_builtin.width_expr != NULL) {
            emit_lit(b, "(size_t)(");
            emit_expr_c(b, ty->u.type_builtin.width_expr, src);
            emit_lit(b, ")");
        }
        else {
            emit_lit(b, "64");
        }
        emit_lit(b, ", &tmp_));\n");
        emit_lit(b, "        self->");
        emit_snake(b, fn, fl);
        emit_lit(b, " = ");
        emit_lit(b, "(");
        emit_element_type(b, ty, src);
        emit_lit(b, ")tmp_;\n");
        emit_lit(b, "    }\n");
        if (has_present) {
            emit_lit(b, "    }\n");
        }
        return;
    }

    if (shape == 'p') {
        emit_lit(b, "    ZSERIO_TRY(");
        emit_lit(b, builtin_read_fn(ty->u.type_builtin.code));
        emit_lit(b, "(bs, &self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "));\n");
    }
    else if (shape == 'o') {
        emit_lit(b, "    ZSERIO_TRY(");
        emit_lit(b, builtin_read_fn(ty->u.type_builtin.code));
        emit_lit(b, "(bs, alloc, &self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "));\n");
    }
    else if (shape == 'c') {
        const zsp_ast_node_t *const decl = peel_alias(ty->resolved);
        emit_lit(b, "    ");
        (void)emit_type_id(b, decl, src);
        emit_lit(b, "_init(&self->");
        emit_snake(b, fn, fl);
        emit_lit(b, ");\n");
        /* Forward this field's `type_args` into the inner
         * struct's `_set_parameters` call so its read/write
         * loops see the correct parameter values. */
        if ((ty->kind == ZSP_AST_TYPE_REF) && (ty->u.type_ref.n_type_args > 0U)) {
            emit_lit(b, "    ");
            (void)emit_type_id(b, decl, src);
            emit_lit(b, "_set_parameters(&self->");
            emit_snake(b, fn, fl);
            for (size_t i = 0U; i < ty->u.type_ref.n_type_args; ++i) {
                emit_lit(b, ", ");
                emit_expr_c(b, ty->u.type_ref.type_args[i], src);
            }
            emit_lit(b, ");\n");
        }
        emit_lit(b, "    ZSERIO_TRY(");
        (void)emit_type_id(b, decl, src);
        emit_lit(b, "_read(&self->");
        emit_snake(b, fn, fl);
        emit_lit(b, ", bs, alloc));\n");
    }
    else if (shape == 'e') {
        const zsp_builtin_code_t uc = enum_underlying(ty);
        const uint32_t           uw = enum_underlying_width(ty);
        emit_lit(b, "    {\n");
        if ((uc == ZSP_TB_BIT_FIELD) || (uc == ZSP_TB_INT_FIELD)) {
            const uint32_t w = (uw > 0U) ? uw : 64U;
            emit_lit(b, "        uint64_t tmp_;\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_read_bits(bs, ");
            emit_uint(b, w);
            emit_lit(b, ", &tmp_));\n");
        }
        else {
            emit_lit(b, "        ");
            emit_lit(b, builtin_c_type(uc));
            emit_lit(b, " tmp_;\n");
            emit_lit(b, "        ZSERIO_TRY(");
            emit_lit(b, builtin_read_fn(uc));
            emit_lit(b, "(bs, &tmp_));\n");
        }
        emit_lit(b, "        self->");
        emit_snake(b, fn, fl);
        emit_lit(b, " = (");
        emit_element_type(b, ty, src);
        emit_lit(b, ")tmp_;\n");
        emit_lit(b, "    }\n");
    }
    else {
        emit_lit(b, "    /* unsupported field type for read: ");
        emit_span(b, fn, fl);
        emit_lit(b, " */\n");
    }

    if (has_present) {
        emit_lit(b, "    }\n");
    }
}

/* Emit a single write for the value at `slot_expr`. */
static void emit_element_write(buf_t *b, const zsp_ast_node_t *ty,
                               const char *slot_expr,
                               const zsp_source_t *src)
{
    const zsp_ast_node_t *type_args_origin = ty;
    while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
            && (ty->resolved != NULL)
            && (ty->resolved->kind == ZSP_AST_SUBTYPE)
            && (ty->resolved->u.subtype.target_type != NULL)) {
        ty = ty->resolved->u.subtype.target_type;
    }
    const char shape = element_read_shape(ty);
    if ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_BUILTIN)
            && ((ty->u.type_builtin.code == ZSP_TB_BIT_FIELD)
                || (ty->u.type_builtin.code == ZSP_TB_INT_FIELD))) {
        const bool is_signed = (ty->u.type_builtin.code == ZSP_TB_INT_FIELD);
        emit_lit(b, is_signed
                ? "            ZSERIO_TRY(zserio_bitstream_write_signed(bs, "
                : "            ZSERIO_TRY(zserio_bitstream_write_bits(bs, ");
        if (ty->u.type_builtin.width > 0U) {
            emit_uint(b, ty->u.type_builtin.width);
        }
        else if (ty->u.type_builtin.width_expr != NULL) {
            emit_lit(b, "(size_t)(");
            emit_expr_c(b, ty->u.type_builtin.width_expr, src);
            emit_lit(b, ")");
        }
        else {
            emit_lit(b, "64");
        }
        emit_lit(b, is_signed ? ", (int64_t)" : ", (uint64_t)");
        emit_lit(b, slot_expr);
        emit_lit(b, "));\n");
        return;
    }
    if (shape == 'p') {
        const zsp_builtin_code_t c = ty->u.type_builtin.code;
        emit_lit(b, "            ZSERIO_TRY(");
        emit_lit(b, builtin_write_fn(c));
        emit_lit(b, "(bs, ");
        emit_lit(b, slot_expr);
        emit_lit(b, "));\n");
    }
    else if (shape == 'o') {
        const zsp_builtin_code_t c = ty->u.type_builtin.code;
        if (c == ZSP_TB_STRING) {
            emit_lit(b, "            {\n");
            emit_lit(b, "                const zserio_string_view_t v_ = { ");
            emit_lit(b, slot_expr);
            emit_lit(b, ".bytes, ");
            emit_lit(b, slot_expr);
            emit_lit(b, ".length };\n");
            emit_lit(b, "                ZSERIO_TRY(zserio_write_string(bs, v_));\n");
            emit_lit(b, "            }\n");
        }
        else if (c == ZSP_TB_BYTES) {
            emit_lit(b, "            {\n");
            emit_lit(b, "                const zserio_bytes_view_t v_ = { ");
            emit_lit(b, slot_expr);
            emit_lit(b, ".bytes, ");
            emit_lit(b, slot_expr);
            emit_lit(b, ".length };\n");
            emit_lit(b, "                ZSERIO_TRY(zserio_write_bytes(bs, v_));\n");
            emit_lit(b, "            }\n");
        }
        else if (c == ZSP_TB_EXTERN) {
            emit_lit(b, "            {\n");
            emit_lit(b, "                const zserio_bit_view_t v_ = { ");
            emit_lit(b, slot_expr);
            emit_lit(b, ".bytes, 0u, ");
            emit_lit(b, slot_expr);
            emit_lit(b, ".bit_length };\n");
            emit_lit(b, "                ZSERIO_TRY(zserio_write_extern(bs, v_));\n");
            emit_lit(b, "            }\n");
        }
        else {
            emit_lit(b, "            ZSERIO_TRY(");
            emit_lit(b, builtin_write_fn(c));
            emit_lit(b, "(bs, &");
            emit_lit(b, slot_expr);
            emit_lit(b, "));\n");
        }
    }
    else if (shape == 'c') {
        const zsp_ast_node_t *const decl = peel_alias(ty->resolved);
        /* Propagate type_args into the nested write so any
         * length/conditional expressions inside the parameterised
         * compound resolve correctly. The cast strips `const` —
         * set_parameters mutates `self->...` but in practice
         * write functions only read those, so the lifetime is
         * scoped to this call. */
        const zsp_ast_node_t *args_src = ty;
        if ((args_src == NULL) || (args_src->kind != ZSP_AST_TYPE_REF)
                || (args_src->u.type_ref.n_type_args == 0U)) {
            args_src = type_args_origin;
        }
        if ((args_src != NULL) && (args_src->kind == ZSP_AST_TYPE_REF)
                && (args_src->u.type_ref.n_type_args > 0U)) {
            emit_lit(b, "            ");
            (void)emit_type_id(b, decl, src);
            emit_lit(b, "_set_parameters((");
            (void)emit_type_id(b, decl, src);
            emit_lit(b, "_t *)&");
            emit_lit(b, slot_expr);
            for (size_t i = 0U; i < args_src->u.type_ref.n_type_args; ++i) {
                emit_lit(b, ", ");
                emit_expr_c(b, args_src->u.type_ref.type_args[i], src);
            }
            emit_lit(b, ");\n");
        }
        emit_lit(b, "            ZSERIO_TRY(");
        (void)emit_type_id(b, decl, src);
        emit_lit(b, "_write(&");
        emit_lit(b, slot_expr);
        emit_lit(b, ", bs));\n");
    }
    else if (shape == 'e') {
        const zsp_builtin_code_t uc = enum_underlying(ty);
        const uint32_t           uw = enum_underlying_width(ty);
        if ((uc == ZSP_TB_BIT_FIELD) || (uc == ZSP_TB_INT_FIELD)) {
            const uint32_t w = (uw > 0U) ? uw : 64U;
            emit_lit(b, "            ZSERIO_TRY(zserio_bitstream_write_bits(bs, ");
            emit_uint(b, w);
            emit_lit(b, ", (uint64_t)");
            emit_lit(b, slot_expr);
            emit_lit(b, "));\n");
        }
        else {
            emit_lit(b, "            ZSERIO_TRY(");
            emit_lit(b, builtin_write_fn(uc));
            emit_lit(b, "(bs, (");
            emit_lit(b, builtin_c_type(uc));
            emit_lit(b, ")");
            emit_lit(b, slot_expr);
            emit_lit(b, "));\n");
        }
    }
}

static void emit_field_write(buf_t *b, const zsp_ast_node_t *field,
                             const zsp_source_t *src,
                             bool referenced_as_offset_source)
{
    const uint8_t *fn = NULL;
    size_t         fl = 0U;
    if (!ident_span(src, field->u.field.name, &fn, &fl)) {
        return;
    }
    const zsp_ast_node_t *ty = field->u.field.type;
    if (ty == NULL) {
        return;
    }
    const bool is_array = (ty->kind == ZSP_AST_TYPE_ARRAY);
    const zsp_ast_node_t *element_ty = is_array ? ty->u.type_array.element : ty;

    /* Field-level alignment + offset directives — only apply
     * when the field is actually written (so when an if-clause
     * is false, no padding is consumed). For unconditional
     * fields, emit them up front; for conditional fields,
     * emit them inside the `if (..._present) { }` body below. */
    const bool has_if = (field->u.field.if_clause != NULL)
            || field->u.field.optional;
    if (!has_if && field->u.field.align_expr != NULL) {
        emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_writer_align(bs, ");
        emit_expr_c(b, field->u.field.align_expr, src);
        emit_lit(b, "));\n");
    }
    /* Offsets back-patch: byte-align, capture the current byte
     * position, seek back to the offset-source field's saved
     * position, overwrite it with the byte index, then seek
     * back to here and continue writing the target. */
    if (!has_if
            && (field->u.field.offset_expr != NULL)
            && (field->u.field.offset_expr->kind == ZSP_AST_EXPR_IDENT)
            && (field->u.field.offset_expr->resolved != NULL)
            && (field->u.field.offset_expr->resolved->kind == ZSP_AST_FIELD)) {
        const zsp_ast_node_t *const src_field = field->u.field.offset_expr->resolved;
        const zsp_ast_node_t *const src_ty    = src_field->u.field.type;
        const uint8_t *sn = NULL;
        size_t         sl = 0U;
        if (ident_span(src, src_field->u.field.name, &sn, &sl)
                && (src_ty != NULL)) {
            emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_writer_align(bs, 8u));\n");
            emit_lit(b, "    {\n");
            emit_lit(b, "        const size_t cur_pos_ = zserio_bitstream_writer_position(bs);\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_writer_seek(bs, ");
            emit_snake(b, sn, sl);
            emit_lit(b, "_off_pos_));\n");
            /* Pick a write call matching the offset-source type. */
            if (src_ty->kind == ZSP_AST_TYPE_BUILTIN) {
                const zsp_builtin_code_t c = src_ty->u.type_builtin.code;
                if ((c == ZSP_TB_BIT_FIELD) || (c == ZSP_TB_INT_FIELD)) {
                    emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_write_bits(bs, ");
                    if (src_ty->u.type_builtin.width > 0U) {
                        emit_uint(b, src_ty->u.type_builtin.width);
                    }
                    else if (src_ty->u.type_builtin.width_expr != NULL) {
                        emit_lit(b, "(size_t)(");
                        emit_expr_c(b, src_ty->u.type_builtin.width_expr, src);
                        emit_lit(b, ")");
                    }
                    else {
                        emit_lit(b, "64");
                    }
                    emit_lit(b, ", (uint64_t)(cur_pos_ / 8u)));\n");
                }
                else {
                    emit_lit(b, "        ZSERIO_TRY(");
                    emit_lit(b, builtin_write_fn(c));
                    emit_lit(b, "(bs, (");
                    emit_lit(b, builtin_c_type(c));
                    emit_lit(b, ")(cur_pos_ / 8u)));\n");
                }
            }
            emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_writer_seek(bs, cur_pos_));\n");
            emit_lit(b, "    }\n");
        }
    }

    if (is_array) {
        const zsp_ast_node_t *const lenexpr = ty->u.type_array.length_expr;
        const zsp_sema_const_t *const lenc = (lenexpr != NULL)
                ? (const zsp_sema_const_t *)lenexpr->computed : NULL;
        const bool fixed = (lenc != NULL) && (lenc->kind == ZSP_CONST_UINT)
                && (lenc->v.u > 0U);
        const zsp_ast_node_t *const if_clause_a = field->u.field.if_clause;
        const bool has_present_a =
                field->u.field.optional || (if_clause_a != NULL);
        if (field->u.field.optional && (if_clause_a == NULL)) {
            emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1u, self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_present ? 1u : 0u));\n");
            emit_lit(b, "    if (self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_present)\n");
        }
        else if (if_clause_a != NULL) {
            emit_lit(b, "    if (");
            emit_expr_c(b, if_clause_a, src);
            emit_lit(b, ")\n");
        }
        (void)has_present_a;
        emit_lit(b, "    {\n");
        if (lenexpr == NULL) {
            /* Auto-array: write length prefix. */
            emit_lit(b, "        ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_count));\n");
            /* Capture _off_pos_ AFTER the varsize so any later
             * field with `offset(<this>[@index])` back-patches
             * the actual first-element position. The outer
             * compound write declared the variable. */
            if (referenced_as_offset_source) {
                emit_lit(b, "        ");
                emit_snake(b, fn, fl);
                emit_lit(b, "_off_pos_ = zserio_bitstream_writer_position(bs);\n");
            }
        }
        emit_lit(b, "        const size_t arr_count_ = ");
        if (fixed) {
            emit_uint(b, lenc->v.u);
            emit_lit(b, "u;\n");
        }
        else if (lenexpr != NULL) {
            emit_lit(b, "(size_t)(");
            emit_expr_c(b, lenexpr, src);
            emit_lit(b, ");\n");
        }
        else {
            emit_lit(b, "self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_count;\n");
        }
        /* Packed-array write protocol: init context, observe each
         * element (so finalise() can pick packed vs unpacked + the
         * max delta bit width), then emit per-element writes via
         * `zserio_delta_write`. The very first write also emits
         * the packing descriptor + the first full element.
         * For compound elements with at least one packable scalar
         * field, use the compound's `_packing_context_t` + its
         * observe/write_packed entry points. */
        packed_elem_t pe_w = {0};
        const bool is_packed_w = ty->u.type_array.packed
                && packed_elem_describe(element_ty, &pe_w);
        const char *varint_traits_w = NULL;
        bool varint_signed_w = false;
        if (ty->u.type_array.packed && !is_packed_w
                && (element_ty != NULL)
                && (element_ty->kind == ZSP_AST_TYPE_BUILTIN)) {
            varint_traits_w = varint_traits_name(
                    element_ty->u.type_builtin.code, &varint_signed_w);
        }
        const zsp_ast_node_t *cmpd_decl_w = NULL;
        if (ty->u.type_array.packed && !is_packed_w
                && (varint_traits_w == NULL)
                && (element_ty != NULL)
                && (element_ty->kind == ZSP_AST_TYPE_REF)) {
            const zsp_ast_node_t *d = peel_alias(element_ty->resolved);
            if ((d != NULL)
                    && ((d->kind == ZSP_AST_STRUCT) || (d->kind == ZSP_AST_UNION))
                    && compound_has_packable_field(d)) {
                cmpd_decl_w = d;
            }
        }
        char slot_w[256];
        if (is_packed_w || (varint_traits_w != NULL) || (cmpd_decl_w != NULL)) {
            size_t spos = 0U;
            const char *prefix = "self->";
            const size_t plen = strlen(prefix);
            (void)memcpy(slot_w, prefix, plen); spos = plen;
            spos = snake_into(slot_w, spos, fn, fl);
            const char *suffix = "[i_]";
            (void)memcpy(&slot_w[spos], suffix, strlen(suffix));
            spos += strlen(suffix);
            slot_w[spos] = '\0';
        }
        if (is_packed_w) {
            emit_lit(b, "        zserio_delta_context_t pctx_;\n");
            emit_lit(b, "        (void)memset(&pctx_, 0, sizeof(pctx_));\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_delta_init(&pctx_, ");
            if (pe_w.num_bits > 0U) {
                emit_uint(b, pe_w.num_bits);
                emit_lit(b, "u");
            } else if (pe_w.width_expr != NULL) {
                emit_lit(b, "(uint8_t)(");
                emit_expr_c(b, pe_w.width_expr, src);
                emit_lit(b, ")");
            } else {
                emit_lit(b, "64u");
            }
            emit_lit(b, ", ");
            emit_lit(b, pe_w.is_signed ? "true" : "false");
            emit_lit(b, "));\n");
            emit_lit(b, "        for (size_t i_ = 0u; i_ < arr_count_; ++i_) {\n");
            emit_lit(b, "            ZSERIO_TRY(zserio_delta_observe(&pctx_, (uint64_t)");
            if (pe_w.is_signed) emit_lit(b, "(int64_t)");
            emit_lit(b, slot_w);
            emit_lit(b, "));\n");
            emit_lit(b, "        }\n");
        } else if (varint_traits_w != NULL) {
            emit_lit(b, "        zserio_delta_context_t pctx_;\n");
            emit_lit(b, "        (void)memset(&pctx_, 0, sizeof(pctx_));\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_delta_init_traits(&pctx_, &");
            emit_lit(b, varint_traits_w);
            emit_lit(b, "));\n");
            emit_lit(b, "        for (size_t i_ = 0u; i_ < arr_count_; ++i_) {\n");
            emit_lit(b, "            ZSERIO_TRY(zserio_delta_observe(&pctx_, (uint64_t)");
            if (varint_signed_w) emit_lit(b, "(int64_t)");
            emit_lit(b, slot_w);
            emit_lit(b, "));\n");
            emit_lit(b, "        }\n");
        } else if (cmpd_decl_w != NULL) {
            emit_lit(b, "        ");
            (void)emit_type_id(b, cmpd_decl_w, src);
            emit_lit(b, "_packing_context_t pctx_;\n");
            emit_lit(b, "        ");
            (void)emit_type_id(b, cmpd_decl_w, src);
            emit_lit(b, "_init_packing_context(&pctx_);\n");
            emit_lit(b, "        for (size_t i_ = 0u; i_ < arr_count_; ++i_) {\n");
            emit_lit(b, "            ");
            (void)emit_type_id(b, cmpd_decl_w, src);
            emit_lit(b, "_observe_packing(&pctx_, &");
            emit_lit(b, slot_w);
            emit_lit(b, ");\n");
            emit_lit(b, "        }\n");
        }
        emit_lit(b, "        for (size_t i_ = 0u; i_ < arr_count_; ++i_) {\n");

        /* Indexed-offsets back-patch: when the array carries an
         * `offset_expr` of the form `<source_arr>[@index]`, each
         * iteration byte-aligns, captures cur_pos, then patches
         * the source-array slot at index `i_` with the byte
         * position. The source array must be a fixed-size
         * (compile-time-constant length) array of a builtin int. */
        if ((field->u.field.offset_expr != NULL)
                && (field->u.field.offset_expr->kind == ZSP_AST_EXPR_INDEX)
                && (field->u.field.offset_expr->u.index.lhs != NULL)
                && (field->u.field.offset_expr->u.index.lhs->resolved != NULL)
                && (field->u.field.offset_expr->u.index.lhs->resolved->kind == ZSP_AST_FIELD)) {
            const zsp_ast_node_t *const src_arr_field =
                    field->u.field.offset_expr->u.index.lhs->resolved;
            const zsp_ast_node_t *const src_arr_ty = src_arr_field->u.field.type;
            const uint8_t *sn = NULL;
            size_t         sl = 0U;
            if ((src_arr_ty != NULL) && (src_arr_ty->kind == ZSP_AST_TYPE_ARRAY)
                    && (src_arr_ty->u.type_array.element != NULL)
                    && (src_arr_ty->u.type_array.element->kind == ZSP_AST_TYPE_BUILTIN)
                    && ident_span(src, src_arr_field->u.field.name, &sn, &sl)) {
                const zsp_ast_node_t *const elem = src_arr_ty->u.type_array.element;
                const zsp_builtin_code_t c = elem->u.type_builtin.code;
                uint32_t elem_bits = 0U;
                switch (c) {
                case ZSP_TB_UINT8: case ZSP_TB_INT8: elem_bits = 8U; break;
                case ZSP_TB_UINT16: case ZSP_TB_INT16: elem_bits = 16U; break;
                case ZSP_TB_UINT32: case ZSP_TB_INT32: elem_bits = 32U; break;
                case ZSP_TB_UINT64: case ZSP_TB_INT64: elem_bits = 64U; break;
                case ZSP_TB_BIT_FIELD: case ZSP_TB_INT_FIELD:
                    elem_bits = elem->u.type_builtin.width;
                    break;
                default: break;
                }
                if (elem_bits > 0U) {
                    emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_writer_align(bs, 8u));\n");
                    emit_lit(b, "        {\n");
                    emit_lit(b, "            const size_t cur_pos_ = zserio_bitstream_writer_position(bs);\n");
                    emit_lit(b, "            ZSERIO_TRY(zserio_bitstream_writer_seek(bs, ");
                    emit_snake(b, sn, sl);
                    emit_lit(b, "_off_pos_ + i_ * ");
                    emit_uint(b, elem_bits);
                    emit_lit(b, "u));\n");
                    if ((c == ZSP_TB_BIT_FIELD) || (c == ZSP_TB_INT_FIELD)) {
                        emit_lit(b, "            ZSERIO_TRY(zserio_bitstream_write_bits(bs, ");
                        emit_uint(b, elem_bits);
                        emit_lit(b, ", (uint64_t)(cur_pos_ / 8u)));\n");
                    }
                    else {
                        emit_lit(b, "            ZSERIO_TRY(");
                        emit_lit(b, builtin_write_fn(c));
                        emit_lit(b, "(bs, (");
                        emit_lit(b, builtin_c_type(c));
                        emit_lit(b, ")(cur_pos_ / 8u)));\n");
                    }
                    emit_lit(b, "            ZSERIO_TRY(zserio_bitstream_writer_seek(bs, cur_pos_));\n");
                    emit_lit(b, "        }\n");
                }
            }
        }

        char slot[256];
        size_t pos = 0U;
        const char *prefix = "self->";
        const size_t plen = strlen(prefix);
        (void)memcpy(slot, prefix, plen); pos = plen;
        pos = snake_into(slot, pos, fn, fl);
        const char *suffix = "[i_]";
        (void)memcpy(&slot[pos], suffix, strlen(suffix));
        pos += strlen(suffix);
        slot[pos] = '\0';

        if (is_packed_w) {
            emit_lit(b, "            ZSERIO_TRY(zserio_delta_write(&pctx_, bs, (uint64_t)");
            if (pe_w.is_signed) emit_lit(b, "(int64_t)");
            emit_lit(b, slot);
            emit_lit(b, "));\n");
        } else if (varint_traits_w != NULL) {
            emit_lit(b, "            ZSERIO_TRY(zserio_delta_write(&pctx_, bs, (uint64_t)");
            if (varint_signed_w) emit_lit(b, "(int64_t)");
            emit_lit(b, slot);
            emit_lit(b, "));\n");
        } else if (cmpd_decl_w != NULL) {
            emit_lit(b, "            ZSERIO_TRY(");
            (void)emit_type_id(b, cmpd_decl_w, src);
            emit_lit(b, "_write_packed(&");
            emit_lit(b, slot);
            emit_lit(b, ", bs, &pctx_));\n");
        } else {
            emit_element_write(b, element_ty, slot, src);
        }

        emit_lit(b, "        }\n");
        emit_lit(b, "    }\n");
        return;
    }

    /* Scalar field. */
    char slot[256];
    size_t pos = 0U;
    const char *prefix = "self->";
    const size_t plen = strlen(prefix);
    (void)memcpy(slot, prefix, plen); pos = plen;
    pos = snake_into(slot, pos, fn, fl);
    slot[pos] = '\0';

    /* If-clause + optional wrapping mirrors the read. A plain
     * `optional T x;` writes its 1-bit "is set" tag from the
     * caller-set `_present` flag, then conditionally writes the
     * payload. An `if`-clause is recomputed from the struct's
     * current state on write — the user just sets the field
     * value; the predicate decides whether bytes get emitted. */
    const zsp_ast_node_t *const if_clause = field->u.field.if_clause;
    const bool has_present =
            field->u.field.optional || (if_clause != NULL);
    if (field->u.field.optional && (if_clause == NULL)) {
        emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1u, self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_present ? 1u : 0u));\n");
        emit_lit(b, "    if (self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_present) {\n");
    }
    else if (if_clause != NULL) {
        emit_lit(b, "    if (");
        emit_expr_c(b, if_clause, src);
        emit_lit(b, ") {\n");
    }
    /* Inside the conditional body emit alignment / offset
     * back-patch so they don't fire when the field is skipped. */
    if (has_present && field->u.field.align_expr != NULL) {
        emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_writer_align(bs, ");
        emit_expr_c(b, field->u.field.align_expr, src);
        emit_lit(b, "));\n");
    }
    if (has_present
            && (field->u.field.offset_expr != NULL)
            && (field->u.field.offset_expr->kind == ZSP_AST_EXPR_IDENT)
            && (field->u.field.offset_expr->resolved != NULL)
            && (field->u.field.offset_expr->resolved->kind == ZSP_AST_FIELD)) {
        const zsp_ast_node_t *const src_field = field->u.field.offset_expr->resolved;
        const zsp_ast_node_t *const src_ty    = src_field->u.field.type;
        const uint8_t *sn = NULL;
        size_t         sl = 0U;
        if (ident_span(src, src_field->u.field.name, &sn, &sl)
                && (src_ty != NULL)) {
            emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_writer_align(bs, 8u));\n");
            emit_lit(b, "    {\n");
            emit_lit(b, "        const size_t cur_pos_ = zserio_bitstream_writer_position(bs);\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_writer_seek(bs, ");
            emit_snake(b, sn, sl);
            emit_lit(b, "_off_pos_));\n");
            if (src_ty->kind == ZSP_AST_TYPE_BUILTIN) {
                const zsp_builtin_code_t c = src_ty->u.type_builtin.code;
                if ((c == ZSP_TB_BIT_FIELD) || (c == ZSP_TB_INT_FIELD)) {
                    emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_write_bits(bs, ");
                    if (src_ty->u.type_builtin.width > 0U) emit_uint(b, src_ty->u.type_builtin.width);
                    else if (src_ty->u.type_builtin.width_expr != NULL) {
                        emit_lit(b, "(size_t)(");
                        emit_expr_c(b, src_ty->u.type_builtin.width_expr, src);
                        emit_lit(b, ")");
                    }
                    else emit_lit(b, "64");
                    emit_lit(b, ", (uint64_t)(cur_pos_ / 8u)));\n");
                }
                else {
                    emit_lit(b, "        ZSERIO_TRY(");
                    emit_lit(b, builtin_write_fn(c));
                    emit_lit(b, "(bs, (");
                    emit_lit(b, builtin_c_type(c));
                    emit_lit(b, ")(cur_pos_ / 8u)));\n");
                }
            }
            emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_writer_seek(bs, cur_pos_));\n");
            emit_lit(b, "    }\n");
        }
    }

    /* Bit:N / bit<expr> write through zserio_bitstream_write_bits.
     * Dynamic-width fields carry a `width_expr` we evaluate on
     * the fly; absent both we fall back to 64-bit. Signed
     * (`int:N`) routes through the runtime's sign-aware helper. */
    if ((element_ty->kind == ZSP_AST_TYPE_BUILTIN)
            && ((element_ty->u.type_builtin.code == ZSP_TB_BIT_FIELD)
                || (element_ty->u.type_builtin.code == ZSP_TB_INT_FIELD))) {
        const bool is_signed = (element_ty->u.type_builtin.code == ZSP_TB_INT_FIELD);
        emit_lit(b, is_signed
                ? "    ZSERIO_TRY(zserio_bitstream_write_signed(bs, "
                : "    ZSERIO_TRY(zserio_bitstream_write_bits(bs, ");
        if (element_ty->u.type_builtin.width > 0U) {
            emit_uint(b, element_ty->u.type_builtin.width);
        }
        else if (element_ty->u.type_builtin.width_expr != NULL) {
            emit_lit(b, "(size_t)(");
            emit_expr_c(b, element_ty->u.type_builtin.width_expr, src);
            emit_lit(b, ")");
        }
        else {
            emit_lit(b, "64");
        }
        emit_lit(b, is_signed ? ", (int64_t)self->" : ", (uint64_t)self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "));\n");
        if (has_present) {
            emit_lit(b, "    }\n");
        }
        return;
    }

    /* `emit_element_write` indents 12 spaces (for the array loop
     * body). For scalar fields we want 4 spaces; emit a small
     * wrapper. */
    emit_lit(b, "    {\n");
    emit_element_write(b, element_ty, slot, src);
    emit_lit(b, "    }\n");

    if (has_present) {
        emit_lit(b, "    }\n");
    }
}

static void emit_field_free(buf_t *b, const zsp_ast_node_t *field,
                            const zsp_source_t *src)
{
    const uint8_t *fn = NULL;
    size_t         fl = 0U;
    if (!ident_span(src, field->u.field.name, &fn, &fl)) {
        return;
    }
    const zsp_ast_node_t *ty = field->u.field.type;
    if (ty == NULL) {
        return;
    }
    const bool is_array = (ty->kind == ZSP_AST_TYPE_ARRAY);
    const zsp_ast_node_t *element_ty = is_array ? ty->u.type_array.element : ty;

    if (is_array) {
        /* Fixed-size arrays are stored inline; free per-element
         * owned content (e.g. strings) but skip the buffer free. */
        const zsp_ast_node_t *const lenexpr = ty->u.type_array.length_expr;
        const zsp_sema_const_t *const lenc = (lenexpr != NULL)
                ? (const zsp_sema_const_t *)lenexpr->computed : NULL;
        const bool fixed = (lenc != NULL) && (lenc->kind == ZSP_CONST_UINT)
                && (lenc->v.u > 0U);
        if (fixed) {
            const char shape = element_read_shape(element_ty);
            if ((shape == 'o') || (shape == 'c')) {
                emit_lit(b, "    for (size_t i_ = 0u; i_ < ");
                emit_uint(b, lenc->v.u);
                emit_lit(b, "u; ++i_) {\n");
                if (shape == 'o') {
                    emit_lit(b, "        ");
                    emit_lit(b, builtin_free_fn(element_ty->u.type_builtin.code));
                    emit_lit(b, "(&self->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, "[i_], alloc);\n");
                }
                else {
                    const zsp_ast_node_t *const decl = peel_alias(element_ty->resolved);
                    emit_lit(b, "        ");
                    (void)emit_type_id(b, decl, src);
                    emit_lit(b, "_free(&self->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, "[i_], alloc);\n");
                }
                emit_lit(b, "    }\n");
            }
            return;
        }
        emit_lit(b, "    if (self->");
        emit_snake(b, fn, fl);
        emit_lit(b, " != NULL) {\n");
        /* Per-element free if owned. */
        const char shape = element_read_shape(element_ty);
        if ((shape == 'o') || (shape == 'c')) {
            emit_lit(b, "        for (size_t i_ = 0u; i_ < self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_count; ++i_) {\n");
            if (shape == 'o') {
                emit_lit(b, "            ");
                emit_lit(b, builtin_free_fn(element_ty->u.type_builtin.code));
                emit_lit(b, "(&self->");
                emit_snake(b, fn, fl);
                emit_lit(b, "[i_], alloc);\n");
            }
            else {
                const zsp_ast_node_t *const decl = peel_alias(element_ty->resolved);
                emit_lit(b, "            ");
                (void)emit_type_id(b, decl, src);
                emit_lit(b, "_free(&self->");
                emit_snake(b, fn, fl);
                emit_lit(b, "[i_], alloc);\n");
            }
            emit_lit(b, "        }\n");
        }
        /* Buffer free. */
        emit_lit(b, "        alloc->vtable->free(alloc->userdata, self->");
        emit_snake(b, fn, fl);
        emit_lit(b, ",\n");
        emit_lit(b, "                self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_count * sizeof(*self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "),\n");
        emit_lit(b, "                sizeof(*self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "));\n");
        emit_lit(b, "        self->");
        emit_snake(b, fn, fl);
        emit_lit(b, " = NULL;\n");
        emit_lit(b, "        self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_count = 0u;\n");
        emit_lit(b, "    }\n");
        return;
    }

    if (ty->kind == ZSP_AST_TYPE_BUILTIN) {
        const zsp_builtin_code_t c = ty->u.type_builtin.code;
        if (builtin_needs_free(c)) {
            emit_lit(b, "    ");
            emit_lit(b, builtin_free_fn(c));
            emit_lit(b, "(&self->");
            emit_snake(b, fn, fl);
            emit_lit(b, ", alloc);\n");
        }
    }
    else if (ty->kind == ZSP_AST_TYPE_REF) {
        const zsp_ast_node_t *const decl = peel_alias(ty->resolved);
        if ((decl != NULL)
                && ((decl->kind == ZSP_AST_STRUCT)
                    || (decl->kind == ZSP_AST_UNION)
                    || (decl->kind == ZSP_AST_CHOICE))) {
            emit_lit(b, "    ");
            (void)emit_type_id(b, decl, src);
            emit_lit(b, "_free(&self->");
            emit_snake(b, fn, fl);
            emit_lit(b, ", alloc);\n");
        }
    }
}

static void emit_source_for_compound(buf_t *b, const zsp_ast_node_t *cmpd,
                                     const zsp_source_t *src)
{
    const uint8_t *name = NULL;
    size_t         nlen = 0U;
    (void)ident_span(src, cmpd->u.compound.name, &name, &nlen);

    emit_lit(b, "/* Auto-generated by zserio-c parser. Do not edit. */\n");
    emit_lit(b, "#include \"");
    emit_snake(b, name, nlen);
    emit_lit(b, ".h\"\n\n");

    /* init */
    emit_lit(b, "void ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_init(");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t *self)\n{\n");
    emit_lit(b, "    (void)memset(self, 0, sizeof(*self));\n");
    emit_lit(b, "}\n\n");

    /* set_parameters */
    if (cmpd->u.compound.n_params > 0U) {
        emit_lit(b, "void ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_set_parameters(");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_t *self");
        for (size_t i = 0U; i < cmpd->u.compound.n_params; ++i) {
            const zsp_ast_node_t *p = cmpd->u.compound.params[i];
            emit_lit(b, ", ");
            bool owned = false;
            (void)emit_field_c_type(b, p->u.parameter.type, src, &owned);
            emit_lit(b, " ");
            const uint8_t *pn = NULL;
            size_t         pl = 0U;
            if (ident_span(src, p->u.parameter.name, &pn, &pl)) {
                emit_snake(b, pn, pl);
            }
        }
        emit_lit(b, ")\n{\n");
        for (size_t i = 0U; i < cmpd->u.compound.n_params; ++i) {
            const zsp_ast_node_t *p = cmpd->u.compound.params[i];
            const uint8_t *pn = NULL;
            size_t         pl = 0U;
            if (!ident_span(src, p->u.parameter.name, &pn, &pl)) continue;
            emit_lit(b, "    self->");
            emit_snake(b, pn, pl);
            emit_lit(b, " = ");
            emit_snake(b, pn, pl);
            emit_lit(b, ";\n");
        }
        emit_lit(b, "}\n\n");
    }

    const bool is_union = (cmpd->kind == ZSP_AST_UNION);

    /* read */
    emit_lit(b, "zserio_status_t ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_read(");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t *self, zserio_bitstream_reader_t *bs,\n"
                "        const zserio_allocator_t *alloc)\n{\n");
    emit_lit(b, "    (void)alloc;\n");
    emit_lit(b, "    (void)self;\n");
    emit_lit(b, "    (void)bs;\n");
    if (is_union) {
        emit_lit(b, "    ZSERIO_TRY(zserio_read_varsize(bs, &self->tag));\n");
        emit_lit(b, "    switch (self->tag) {\n");
        size_t arm = 0U;
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if (m->kind != ZSP_AST_FIELD) continue;
            emit_lit(b, "    case ");
            emit_uint(b, (uint64_t)arm);
            emit_lit(b, "u:\n");
            emit_choice_case_read(b, m, src);
            emit_lit(b, "        break;\n");
            arm++;
        }
        emit_lit(b, "    default: break;\n");
        emit_lit(b, "    }\n");
    }
    else {
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if (m->kind != ZSP_AST_FIELD) continue;
            const zsp_ast_node_t *ty = m->u.field.type;
            const bool is_self_rec =
                    (ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                    && (ty->resolved == cmpd);
            if (is_self_rec) {
                /* Self-recursive field is stored as a pointer.
                 * Read: allocate via the runtime allocator, set
                 * parameters from the field's type_args (if any),
                 * then recurse. Guarded by the optional/if-clause
                 * present flag — Block-style chains terminate
                 * by setting the predicate false. */
                const uint8_t *rfn = NULL;
                size_t         rfl = 0U;
                if (!ident_span(src, m->u.field.name, &rfn, &rfl)) continue;
                const zsp_ast_node_t *const if_clause = m->u.field.if_clause;
                if (m->u.field.optional && (if_clause == NULL)) {
                    emit_lit(b, "    {\n        uint64_t opt_tmp_; ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1u, &opt_tmp_));\n");
                    emit_lit(b, "        self->");
                    emit_snake(b, rfn, rfl);
                    emit_lit(b, "_present = (opt_tmp_ != 0u);\n    }\n");
                }
                else if (if_clause != NULL) {
                    emit_lit(b, "    self->");
                    emit_snake(b, rfn, rfl);
                    emit_lit(b, "_present = (");
                    emit_expr_c(b, if_clause, src);
                    emit_lit(b, ");\n");
                }
                emit_lit(b, "    if (self->");
                emit_snake(b, rfn, rfl);
                emit_lit(b, "_present) {\n");
                emit_lit(b, "        self->");
                emit_snake(b, rfn, rfl);
                emit_lit(b, " = (");
                (void)emit_type_id(b, cmpd, src);
                emit_lit(b, "_t *)alloc->vtable->alloc(alloc->userdata, sizeof(*self->");
                emit_snake(b, rfn, rfl);
                emit_lit(b, "), sizeof(*self->");
                emit_snake(b, rfn, rfl);
                emit_lit(b, "));\n");
                emit_lit(b, "        if (self->");
                emit_snake(b, rfn, rfl);
                emit_lit(b, " == NULL) return ZSERIO_ERR_OUT_OF_MEMORY;\n");
                emit_lit(b, "        ");
                (void)emit_type_id(b, cmpd, src);
                emit_lit(b, "_init(self->");
                emit_snake(b, rfn, rfl);
                emit_lit(b, ");\n");
                if ((ty->kind == ZSP_AST_TYPE_REF) && (ty->u.type_ref.n_type_args > 0U)) {
                    emit_lit(b, "        ");
                    (void)emit_type_id(b, cmpd, src);
                    emit_lit(b, "_set_parameters(self->");
                    emit_snake(b, rfn, rfl);
                    for (size_t k = 0U; k < ty->u.type_ref.n_type_args; ++k) {
                        emit_lit(b, ", ");
                        emit_expr_c(b, ty->u.type_ref.type_args[k], src);
                    }
                    emit_lit(b, ");\n");
                }
                emit_lit(b, "        ZSERIO_TRY(");
                (void)emit_type_id(b, cmpd, src);
                emit_lit(b, "_read(self->");
                emit_snake(b, rfn, rfl);
                emit_lit(b, ", bs, alloc));\n");
                emit_lit(b, "    }\n");
                continue;
            }
            if (m->u.field.extend) {
                /* extend fields are read only if the bitstream
                 * has more bytes — otherwise the field stays at
                 * its zero-initialised default (or the schema-
                 * level `= <expr>` init). Mirror the writer's
                 * byte-align before the read. */
                emit_lit(b, "    if (zserio_bitstream_reader_position(bs) < zserio_bitstream_reader_size(bs)) {\n");
                emit_lit(b, "        ZSERIO_TRY(zserio_bitstream_reader_align(bs, 8u));\n");
                emit_field_read(b, m, src);
                emit_lit(b, "    }\n");
                continue;
            }
            emit_field_read(b, m, src);
        }
    }
    emit_lit(b, "    return ZSERIO_OK;\n");
    emit_lit(b, "}\n\n");

    /* write */
    emit_lit(b, "zserio_status_t ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_write(const ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t *self, zserio_bitstream_writer_t *bs)\n{\n");
    emit_lit(b, "    (void)self;\n");
    emit_lit(b, "    (void)bs;\n");
    if (is_union) {
        emit_lit(b, "    ZSERIO_TRY(zserio_write_varsize(bs, self->tag));\n");
        emit_lit(b, "    switch (self->tag) {\n");
        size_t arm = 0U;
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if (m->kind != ZSP_AST_FIELD) continue;
            emit_lit(b, "    case ");
            emit_uint(b, (uint64_t)arm);
            emit_lit(b, "u:\n");
            emit_choice_case_write(b, m, src);
            emit_lit(b, "        break;\n");
            arm++;
        }
        emit_lit(b, "    default: break;\n");
        emit_lit(b, "    }\n");
    }
    else {
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if (m->kind != ZSP_AST_FIELD) continue;
            const zsp_ast_node_t *ty = m->u.field.type;
            const bool is_self_rec =
                    (ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                    && (ty->resolved == cmpd);
            if (is_self_rec) {
                /* Mirror of the read path: when the
                 * caller-controlled `_present` flag is set,
                 * dereference the pointer and recurse. */
                const uint8_t *wfn = NULL;
                size_t         wfl = 0U;
                if (!ident_span(src, m->u.field.name, &wfn, &wfl)) continue;
                if (m->u.field.optional && (m->u.field.if_clause == NULL)) {
                    emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1u, self->");
                    emit_snake(b, wfn, wfl);
                    emit_lit(b, "_present ? 1u : 0u));\n");
                }
                emit_lit(b, "    if (self->");
                emit_snake(b, wfn, wfl);
                emit_lit(b, "_present) {\n");
                if ((ty->kind == ZSP_AST_TYPE_REF) && (ty->u.type_ref.n_type_args > 0U)) {
                    emit_lit(b, "        ");
                    (void)emit_type_id(b, cmpd, src);
                    emit_lit(b, "_set_parameters(self->");
                    emit_snake(b, wfn, wfl);
                    for (size_t k = 0U; k < ty->u.type_ref.n_type_args; ++k) {
                        emit_lit(b, ", ");
                        emit_expr_c(b, ty->u.type_ref.type_args[k], src);
                    }
                    emit_lit(b, ");\n");
                }
                emit_lit(b, "        ZSERIO_TRY(");
                (void)emit_type_id(b, cmpd, src);
                emit_lit(b, "_write(self->");
                emit_snake(b, wfn, wfl);
                emit_lit(b, ", bs));\n    }\n");
                continue;
            }
            /* Offsets feature: if any LATER field references this
             * field via its `offset(<name>)` directive (or the
             * indexed variant `offset(<name>[@index])`), emit a
             * save of the writer position so the back-patch in
             * the target field's write can rewind here and
             * overwrite the offset value. */
            const uint8_t *mn = NULL;
            size_t         ml = 0U;
            if (ident_span(src, m->u.field.name, &mn, &ml)) {
                bool referenced = false;
                for (size_t j = i + 1U; j < cmpd->u.compound.n_members; ++j) {
                    const zsp_ast_node_t *o = cmpd->u.compound.members[j];
                    if ((o == NULL) || (o->kind != ZSP_AST_FIELD)) continue;
                    const zsp_ast_node_t *oe = o->u.field.offset_expr;
                    if (oe == NULL) continue;
                    if ((oe->kind == ZSP_AST_EXPR_IDENT)
                            && (oe->resolved == m)) {
                        referenced = true;
                        break;
                    }
                    if ((oe->kind == ZSP_AST_EXPR_INDEX)
                            && (oe->u.index.lhs != NULL)
                            && (oe->u.index.lhs->resolved == m)) {
                        referenced = true;
                        break;
                    }
                }
                if (referenced) {
                    /* Auto-arrays write a varsize length-prefix
                     * BEFORE their first element; the back-patch
                     * target is the position of element 0, not
                     * the position of the prefix. Declare the
                     * variable now (non-const) and let
                     * emit_field_write reassign it after the
                     * varsize write. Fixed arrays / scalars
                     * remain captured here, which is correct
                     * since their first byte is at this position. */
                    const zsp_ast_node_t *mty = m->u.field.type;
                    const bool is_auto_arr = (mty != NULL)
                            && (mty->kind == ZSP_AST_TYPE_ARRAY)
                            && (mty->u.type_array.length_expr == NULL);
                    if (is_auto_arr) {
                        emit_lit(b, "    size_t ");
                        emit_snake(b, mn, ml);
                        emit_lit(b, "_off_pos_ = 0u;\n");
                    } else {
                        emit_lit(b, "    const size_t ");
                        emit_snake(b, mn, ml);
                        emit_lit(b, "_off_pos_ = zserio_bitstream_writer_position(bs);\n");
                    }
                }
            }
            if (m->u.field.extend) {
                /* extend fields are unconditionally written and
                 * always byte-aligned before. */
                emit_lit(b, "    ZSERIO_TRY(zserio_bitstream_writer_align(bs, 8u));\n");
            }
            bool field_referenced = false;
            {
                const uint8_t *fn2 = NULL;
                size_t fl2 = 0U;
                if (ident_span(src, m->u.field.name, &fn2, &fl2)) {
                    for (size_t j = i + 1U; j < cmpd->u.compound.n_members; ++j) {
                        const zsp_ast_node_t *o = cmpd->u.compound.members[j];
                        if ((o == NULL) || (o->kind != ZSP_AST_FIELD)) continue;
                        const zsp_ast_node_t *oe = o->u.field.offset_expr;
                        if (oe == NULL) continue;
                        if (((oe->kind == ZSP_AST_EXPR_IDENT)
                                    && (oe->resolved == m))
                                || ((oe->kind == ZSP_AST_EXPR_INDEX)
                                    && (oe->u.index.lhs != NULL)
                                    && (oe->u.index.lhs->resolved == m))) {
                            field_referenced = true;
                            break;
                        }
                    }
                }
                (void)fn2; (void)fl2;
            }
            emit_field_write(b, m, src, field_referenced);
        }
    }
    emit_lit(b, "    return ZSERIO_OK;\n");
    emit_lit(b, "}\n\n");

    /* free */
    emit_lit(b, "void ");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_free(");
    (void)emit_type_id(b, cmpd, src);
    emit_lit(b, "_t *self, const zserio_allocator_t *alloc)\n{\n");
    emit_lit(b, "    (void)self;\n");
    emit_lit(b, "    (void)alloc;\n");
    if (is_union) {
        emit_lit(b, "    switch (self->tag) {\n");
        size_t arm = 0U;
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if (m->kind != ZSP_AST_FIELD) continue;
            emit_lit(b, "    case ");
            emit_uint(b, (uint64_t)arm);
            emit_lit(b, "u:\n");
            emit_choice_case_free(b, m, src);
            emit_lit(b, "        break;\n");
            arm++;
        }
        emit_lit(b, "    default: break;\n");
        emit_lit(b, "    }\n");
    }
    else {
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if (m->kind != ZSP_AST_FIELD) continue;
            const zsp_ast_node_t *ty = m->u.field.type;
            const bool is_self_rec =
                    (ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                    && (ty->resolved == cmpd);
            if (is_self_rec) {
                /* Recursively free + deallocate the linked node. */
                const uint8_t *ffn = NULL;
                size_t         ffl = 0U;
                if (!ident_span(src, m->u.field.name, &ffn, &ffl)) continue;
                emit_lit(b, "    if (self->");
                emit_snake(b, ffn, ffl);
                emit_lit(b, " != NULL) {\n");
                emit_lit(b, "        ");
                (void)emit_type_id(b, cmpd, src);
                emit_lit(b, "_free(self->");
                emit_snake(b, ffn, ffl);
                emit_lit(b, ", alloc);\n");
                emit_lit(b, "        alloc->vtable->free(alloc->userdata, self->");
                emit_snake(b, ffn, ffl);
                emit_lit(b, ", sizeof(*self->");
                emit_snake(b, ffn, ffl);
                emit_lit(b, "), sizeof(*self->");
                emit_snake(b, ffn, ffl);
                emit_lit(b, "));\n");
                emit_lit(b, "        self->");
                emit_snake(b, ffn, ffl);
                emit_lit(b, " = NULL;\n    }\n");
                continue;
            }
            emit_field_free(b, m, src);
        }
    }
    emit_lit(b, "}\n");

    /* Packing-protocol implementations (only if relevant). */
    if (compound_has_packable_field(cmpd)) {
        /* _init_packing_context */
        emit_lit(b, "\nvoid ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_init_packing_context(");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_packing_context_t *ctx)\n{\n");
        emit_lit(b, "    (void)memset(ctx, 0, sizeof(*ctx));\n");
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
            packed_elem_t pe = {0};
            const char cls = field_packing_class(m, &pe);
            const uint8_t *fn = NULL;
            size_t         fl = 0U;
            if (!ident_span(src, m->u.field.name, &fn, &fl)) continue;
            if (cls == 'p') {
                emit_lit(b, "    (void)zserio_delta_init(&ctx->");
                emit_snake(b, fn, fl);
                emit_lit(b, "_, ");
                if (pe.num_bits > 0U) {
                    emit_uint(b, pe.num_bits);
                    emit_lit(b, "u");
                } else if (pe.width_expr != NULL) {
                    /* Dynamic-width fields can't be initialised at
                     * parent-init time (the width comes from a
                     * sibling field that hasn't been read/written
                     * yet). The delta context allows a deferred
                     * init via num_bits==0 sentinel; the inline
                     * read/write call site will re-init with the
                     * concrete width. */
                    emit_lit(b, "0u");
                } else {
                    emit_lit(b, "64u");
                }
                emit_lit(b, ", ");
                emit_lit(b, pe.is_signed ? "true" : "false");
                emit_lit(b, ");\n");
            } else if (cls == 'v') {
                const zsp_ast_node_t *ty_b = m->u.field.type;
                while ((ty_b != NULL) && (ty_b->kind == ZSP_AST_TYPE_REF)
                        && (ty_b->resolved != NULL)
                        && (ty_b->resolved->kind == ZSP_AST_SUBTYPE)
                        && (ty_b->resolved->u.subtype.target_type != NULL)) {
                    ty_b = ty_b->resolved->u.subtype.target_type;
                }
                const char *traits = (ty_b && ty_b->kind == ZSP_AST_TYPE_BUILTIN)
                        ? varint_traits_name(ty_b->u.type_builtin.code, NULL)
                        : NULL;
                if (traits != NULL) {
                    emit_lit(b, "    (void)zserio_delta_init_traits(&ctx->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, "_, &");
                    emit_lit(b, traits);
                    emit_lit(b, ");\n");
                }
            } else if (cls == 'n') {
                const zsp_ast_node_t *ty = m->u.field.type;
                while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                        && (ty->resolved != NULL)
                        && (ty->resolved->kind == ZSP_AST_SUBTYPE)
                        && (ty->resolved->u.subtype.target_type != NULL)) {
                    ty = ty->resolved->u.subtype.target_type;
                }
                if ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                        && (ty->resolved != NULL)
                        && (ty->resolved != cmpd)
                        && compound_has_packable_field(ty->resolved)) {
                    emit_lit(b, "    ");
                    (void)emit_type_id(b, ty->resolved, src);
                    emit_lit(b, "_init_packing_context(&ctx->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, "_);\n");
                }
            }
        }
        emit_lit(b, "}\n");

        /* _observe_packing */
        emit_lit(b, "\nvoid ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_observe_packing(");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_packing_context_t *ctx, const ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_t *self)\n{\n");
        emit_lit(b, "    (void)ctx;\n    (void)self;\n");
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
            packed_elem_t pe = {0};
            const char cls = field_packing_class(m, &pe);
            const uint8_t *fn = NULL;
            size_t         fl = 0U;
            if (!ident_span(src, m->u.field.name, &fn, &fl)) continue;
            if ((cls == 'p') || (cls == 'v')) {
                emit_lit(b, "    (void)zserio_delta_observe(&ctx->");
                emit_snake(b, fn, fl);
                emit_lit(b, "_, (uint64_t)");
                if (pe.is_signed) emit_lit(b, "(int64_t)");
                emit_lit(b, "self->");
                emit_snake(b, fn, fl);
                emit_lit(b, ");\n");
            } else if (cls == 'n') {
                const zsp_ast_node_t *ty = m->u.field.type;
                while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                        && (ty->resolved != NULL)
                        && (ty->resolved->kind == ZSP_AST_SUBTYPE)
                        && (ty->resolved->u.subtype.target_type != NULL)) {
                    ty = ty->resolved->u.subtype.target_type;
                }
                if ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                        && (ty->resolved != NULL)
                        && (ty->resolved != cmpd)
                        && compound_has_packable_field(ty->resolved)) {
                    emit_lit(b, "    ");
                    (void)emit_type_id(b, ty->resolved, src);
                    emit_lit(b, "_observe_packing(&ctx->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, "_, &self->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, ");\n");
                }
            }
        }
        emit_lit(b, "}\n");

        /* _read_packed */
        emit_lit(b, "\nzserio_status_t ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_read_packed(");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_t *self, zserio_bitstream_reader_t *bs,\n"
                    "        const zserio_allocator_t *alloc, ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_packing_context_t *ctx)\n{\n");
        emit_lit(b, "    (void)alloc;\n    (void)self;\n    (void)bs;\n    (void)ctx;\n");
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
            packed_elem_t pe = {0};
            const char cls = field_packing_class(m, &pe);
            const uint8_t *fn = NULL;
            size_t         fl = 0U;
            if (!ident_span(src, m->u.field.name, &fn, &fl)) continue;
            if ((cls == 'p') || (cls == 'v')) {
                emit_lit(b, "    {\n");
                emit_lit(b, "        uint64_t tmp_;\n");
                emit_lit(b, "        ZSERIO_TRY(zserio_delta_read(&ctx->");
                emit_snake(b, fn, fl);
                emit_lit(b, "_, bs, &tmp_));\n");
                emit_lit(b, "        self->");
                emit_snake(b, fn, fl);
                emit_lit(b, " = (");
                emit_element_type(b, m->u.field.type, src);
                emit_lit(b, ")tmp_;\n");
                emit_lit(b, "    }\n");
            } else if (cls == 'n') {
                const zsp_ast_node_t *ty = m->u.field.type;
                while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                        && (ty->resolved != NULL)
                        && (ty->resolved->kind == ZSP_AST_SUBTYPE)
                        && (ty->resolved->u.subtype.target_type != NULL)) {
                    ty = ty->resolved->u.subtype.target_type;
                }
                const zsp_ast_node_t *decl = (ty && ty->kind == ZSP_AST_TYPE_REF)
                        ? ty->resolved : NULL;
                if ((decl != NULL) && (decl != cmpd)
                        && compound_has_packable_field(decl)) {
                    emit_lit(b, "    ");
                    (void)emit_type_id(b, decl, src);
                    emit_lit(b, "_init(&self->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, ");\n");
                    emit_lit(b, "    ZSERIO_TRY(");
                    (void)emit_type_id(b, decl, src);
                    emit_lit(b, "_read_packed(&self->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, ", bs, alloc, &ctx->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, "_));\n");
                } else {
                    emit_field_read(b, m, src);
                }
            } else {
                /* unpackable — regular field read */
                emit_field_read(b, m, src);
            }
        }
        emit_lit(b, "    return ZSERIO_OK;\n}\n");

        /* _write_packed */
        emit_lit(b, "\nzserio_status_t ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_write_packed(const ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_t *self, zserio_bitstream_writer_t *bs, ");
        (void)emit_type_id(b, cmpd, src);
        emit_lit(b, "_packing_context_t *ctx)\n{\n");
        emit_lit(b, "    (void)self;\n    (void)bs;\n    (void)ctx;\n");
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            const zsp_ast_node_t *m = cmpd->u.compound.members[i];
            if ((m == NULL) || (m->kind != ZSP_AST_FIELD)) continue;
            packed_elem_t pe = {0};
            const char cls = field_packing_class(m, &pe);
            const uint8_t *fn = NULL;
            size_t         fl = 0U;
            if (!ident_span(src, m->u.field.name, &fn, &fl)) continue;
            if ((cls == 'p') || (cls == 'v')) {
                emit_lit(b, "    ZSERIO_TRY(zserio_delta_write(&ctx->");
                emit_snake(b, fn, fl);
                emit_lit(b, "_, bs, (uint64_t)");
                if (pe.is_signed) emit_lit(b, "(int64_t)");
                emit_lit(b, "self->");
                emit_snake(b, fn, fl);
                emit_lit(b, "));\n");
            } else if (cls == 'n') {
                const zsp_ast_node_t *ty = m->u.field.type;
                while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                        && (ty->resolved != NULL)
                        && (ty->resolved->kind == ZSP_AST_SUBTYPE)
                        && (ty->resolved->u.subtype.target_type != NULL)) {
                    ty = ty->resolved->u.subtype.target_type;
                }
                const zsp_ast_node_t *decl = (ty && ty->kind == ZSP_AST_TYPE_REF)
                        ? ty->resolved : NULL;
                if ((decl != NULL) && (decl != cmpd)
                        && compound_has_packable_field(decl)) {
                    emit_lit(b, "    ZSERIO_TRY(");
                    (void)emit_type_id(b, decl, src);
                    emit_lit(b, "_write_packed(&self->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, ", bs, &ctx->");
                    emit_snake(b, fn, fl);
                    emit_lit(b, "_));\n");
                } else {
                    emit_field_write(b, m, src, false);
                }
            } else {
                emit_field_write(b, m, src, false);
            }
        }
        emit_lit(b, "    return ZSERIO_OK;\n}\n");
    }
}

/* ----- Choice emission --------------------------------------- */

/* Choice struct shape: parameter members + union of case fields. */
static void emit_choice_decl(buf_t *b, const zsp_ast_node_t *ch,
                             const zsp_source_t *src)
{
    /* Use a `_s` tag suffix on the struct so the typedef can be
     * forward-declared (necessary for self-referential structs
     * like `struct Node { Node next[]; }`). The `_s` suffix
     * avoids C-keyword clashes for schemas with type names like
     * `Float`, `Double`, `Long`, etc. */
    emit_lit(b, "typedef struct ");
    (void)emit_type_id(b, ch, src);
    emit_lit(b, "_s ");
    (void)emit_type_id(b, ch, src);
    emit_lit(b, "_t;\n");
    emit_lit(b, "struct ");
    (void)emit_type_id(b, ch, src);
    emit_lit(b, "_s {\n");

    /* Parameters. */
    for (size_t i = 0U; i < ch->u.choice.n_params; ++i) {
        const zsp_ast_node_t *p = ch->u.choice.params[i];
        emit_lit(b, "    ");
        bool owned = false;
        (void)emit_field_c_type(b, p->u.parameter.type, src, &owned);
        emit_lit(b, " ");
        const uint8_t *pn = NULL;
        size_t         pl = 0U;
        if (ident_span(src, p->u.parameter.name, &pn, &pl)) {
            emit_snake(b, pn, pl);
        }
        emit_lit(b, ";  /* parameter */\n");
    }

    /* Per-case array `_count` siblings live OUTSIDE the union
     * so the caller can set count before assigning the pointer
     * (the union members share storage; we can't access the
     * count through `u.<name>_count` once another arm is set). */
    for (size_t i = 0U; i < ch->u.choice.n_cases; ++i) {
        const zsp_ast_node_t *c = ch->u.choice.cases[i];
        const zsp_ast_node_t *f = c->u.choice_case.field;
        if (f == NULL) continue;
        const zsp_ast_node_t *ty = f->u.field.type;
        const bool is_array = (ty != NULL) && (ty->kind == ZSP_AST_TYPE_ARRAY);
        if (!is_array) continue;
        const uint8_t *fn = NULL;
        size_t         fl = 0U;
        if (!ident_span(src, f->u.field.name, &fn, &fl)) continue;
        emit_lit(b, "    size_t ");
        emit_snake(b, fn, fl);
        emit_lit(b, "_count;\n");
    }
    if (ch->u.choice.default_case != NULL) {
        const zsp_ast_node_t *f =
                ch->u.choice.default_case->u.choice_default.field;
        if ((f != NULL) && (f->u.field.type != NULL)
                && (f->u.field.type->kind == ZSP_AST_TYPE_ARRAY)) {
            const uint8_t *fn = NULL;
            size_t         fl = 0U;
            if (ident_span(src, f->u.field.name, &fn, &fl)) {
                emit_lit(b, "    size_t ");
                emit_snake(b, fn, fl);
                emit_lit(b, "_count;\n");
            }
        }
    }
    /* Union of case fields. */
    emit_lit(b, "    union {\n");
    for (size_t i = 0U; i < ch->u.choice.n_cases; ++i) {
        const zsp_ast_node_t *c = ch->u.choice.cases[i];
        const zsp_ast_node_t *f = c->u.choice_case.field;
        if (f == NULL) continue;
        emit_lit(b, "        ");
        bool owned = false;
        const zsp_ast_node_t *ty = f->u.field.type;
        const bool is_array = (ty != NULL) && (ty->kind == ZSP_AST_TYPE_ARRAY);
        const zsp_ast_node_t *element_ty = is_array ? ty->u.type_array.element : ty;
        (void)emit_field_c_type(b, element_ty, src, &owned);
        if (is_array) {
            emit_lit(b, " *");
        }
        else {
            emit_lit(b, " ");
        }
        const uint8_t *fn = NULL;
        size_t         fl = 0U;
        if (ident_span(src, f->u.field.name, &fn, &fl)) {
            emit_snake(b, fn, fl);
        }
        emit_lit(b, ";\n");
    }
    /* Default arm. */
    if (ch->u.choice.default_case != NULL) {
        const zsp_ast_node_t *f = ch->u.choice.default_case->u.choice_default.field;
        if (f != NULL) {
            emit_lit(b, "        ");
            bool owned = false;
            const zsp_ast_node_t *ty = f->u.field.type;
            const bool is_array = (ty != NULL) && (ty->kind == ZSP_AST_TYPE_ARRAY);
            const zsp_ast_node_t *element_ty = is_array ? ty->u.type_array.element : ty;
            (void)emit_field_c_type(b, element_ty, src, &owned);
            emit_lit(b, is_array ? " *" : " ");
            const uint8_t *fn = NULL;
            size_t         fl = 0U;
            if (ident_span(src, f->u.field.name, &fn, &fl)) {
                emit_snake(b, fn, fl);
            }
            emit_lit(b, ";\n");
        }
    }
    emit_lit(b, "    } u;\n");

    emit_lit(b, "};\n");
}

static void emit_choice_protos(buf_t *b, const zsp_ast_node_t *ch,
                               const zsp_source_t *src)
{
    emit_lit(b, "\nvoid ");      (void)emit_type_id(b, ch, src); emit_lit(b, "_init(");
    (void)emit_type_id(b, ch, src); emit_lit(b, "_t *self);\n");

    if (ch->u.choice.n_params > 0U) {
        emit_lit(b, "\nvoid "); (void)emit_type_id(b, ch, src);
        emit_lit(b, "_set_parameters("); (void)emit_type_id(b, ch, src);
        emit_lit(b, "_t *self");
        for (size_t i = 0U; i < ch->u.choice.n_params; ++i) {
            const zsp_ast_node_t *p = ch->u.choice.params[i];
            emit_lit(b, ", ");
            bool owned = false;
            (void)emit_field_c_type(b, p->u.parameter.type, src, &owned);
            emit_lit(b, " ");
            const uint8_t *pn = NULL;
            size_t         pl = 0U;
            if (ident_span(src, p->u.parameter.name, &pn, &pl)) {
                emit_snake(b, pn, pl);
            }
        }
        emit_lit(b, ");\n");
    }

    emit_lit(b, "\nzserio_status_t "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_read("); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_t *self, zserio_bitstream_reader_t *bs,\n"
                "        const zserio_allocator_t *alloc);\n");

    emit_lit(b, "\nzserio_status_t "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_write(const "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_t *self, zserio_bitstream_writer_t *bs);\n");

    emit_lit(b, "\nvoid "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_free("); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_t *self, const zserio_allocator_t *alloc);\n");
}

/* For one case body, emit the read for the active field. */
static void emit_choice_case_read(buf_t *b, const zsp_ast_node_t *f,
                                  const zsp_source_t *src)
{
    if (f == NULL) {
        return;
    }
    const zsp_ast_node_t *ty = f->u.field.type;
    if (ty == NULL) return;
    const uint8_t *fn = NULL;
    size_t         fl = 0U;
    if (!ident_span(src, f->u.field.name, &fn, &fl)) return;
    /* For simplicity reuse the per-field-read logic by faking
     * a `slot` pointing into `self->u.<field>`. */
    char slot[256];
    size_t pos = 0U;
    const char *prefix = "self->u.";
    const size_t plen = strlen(prefix);
    (void)memcpy(slot, prefix, plen); pos = plen;
    pos = snake_into(slot, pos, fn, fl);
    slot[pos] = '\0';

    /* Array case: emit length-prefix + alloc + per-element read.
     * The array's count sibling lives at `self-><name>_count`
     * (outside the union); the pointer is `self->u.<name>`. */
    if (ty->kind == ZSP_AST_TYPE_ARRAY) {
        const zsp_ast_node_t *const lenexpr = ty->u.type_array.length_expr;
        const zsp_ast_node_t *element_ty = ty->u.type_array.element;
        char arr_slot[256];
        size_t aspos = 0U;
        (void)memcpy(arr_slot, "self->u.", 8U); aspos = 8U;
        aspos = snake_into(arr_slot, aspos, fn, fl);
        const char *suf = "[i_]";
        (void)memcpy(&arr_slot[aspos], suf, strlen(suf));
        aspos += strlen(suf);
        arr_slot[aspos] = '\0';
        emit_lit(b, "        {\n");
        emit_lit(b, "        size_t arr_count_ = 0u;\n");
        if (lenexpr != NULL) {
            emit_lit(b, "        arr_count_ = (size_t)(");
            emit_expr_c(b, lenexpr, src);
            emit_lit(b, ");\n");
        } else {
            emit_lit(b, "        uint32_t arr_len_ = 0u;\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_read_varsize(bs, &arr_len_));\n");
            emit_lit(b, "        arr_count_ = (size_t)arr_len_;\n");
        }
        emit_lit(b, "        self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_count = arr_count_;\n");
        emit_lit(b, "        self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, " = NULL;\n");
        emit_lit(b, "        if (arr_count_ > 0u) {\n");
        emit_lit(b, "            self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, " = (");
        emit_element_type(b, element_ty, src);
        emit_lit(b, " *)alloc->vtable->alloc(alloc->userdata,\n");
        emit_lit(b, "                    arr_count_ * sizeof(*self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, "), sizeof(*self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, "));\n");
        emit_lit(b, "            if (self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, " == NULL) return ZSERIO_ERR_OUT_OF_MEMORY;\n");
        emit_lit(b, "            (void)memset(self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, ", 0, arr_count_ * sizeof(*self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, "));\n");
        emit_lit(b, "        for (size_t i_ = 0u; i_ < arr_count_; ++i_) {\n");
        emit_element_read(b, element_ty, arr_slot, src);
        emit_lit(b, "        }\n");
        emit_lit(b, "        }\n");
        emit_lit(b, "        }\n");
        return;
    }
    /* Peel SUBTYPE aliases so a choice case `subtype int8 Black;
     * black` reads/writes as int8 rather than failing on a
     * TYPE_REF whose `.u.type_builtin` is garbage. */
    {
        const zsp_ast_node_t *peeled = ty;
        while ((peeled != NULL) && (peeled->kind == ZSP_AST_TYPE_REF)
                && (peeled->resolved != NULL)
                && (peeled->resolved->kind == ZSP_AST_SUBTYPE)
                && (peeled->resolved->u.subtype.target_type != NULL)) {
            peeled = peeled->resolved->u.subtype.target_type;
        }
        ty = peeled;
    }
    /* Bit/int fields inside choice cases need the same special
     * casing as field reads outside the choice. */
    if ((ty->kind == ZSP_AST_TYPE_BUILTIN)
            && ((ty->u.type_builtin.code == ZSP_TB_BIT_FIELD)
                || (ty->u.type_builtin.code == ZSP_TB_INT_FIELD))) {
        const bool is_signed = (ty->u.type_builtin.code == ZSP_TB_INT_FIELD);
        emit_lit(b, "{ ");
        emit_lit(b, is_signed ? "int64_t tmp_; " : "uint64_t tmp_; ");
        emit_lit(b, is_signed
                ? "ZSERIO_TRY(zserio_bitstream_read_signed(bs, "
                : "ZSERIO_TRY(zserio_bitstream_read_bits(bs, ");
        if (ty->u.type_builtin.width > 0U) {
            emit_uint(b, ty->u.type_builtin.width);
        }
        else if (ty->u.type_builtin.width_expr != NULL) {
            emit_lit(b, "(size_t)(");
            emit_expr_c(b, ty->u.type_builtin.width_expr, src);
            emit_lit(b, ")");
        }
        else {
            emit_lit(b, "64");
        }
        emit_lit(b, ", &tmp_)); ");
        emit_lit(b, slot);
        emit_lit(b, " = ("); emit_element_type(b, ty, src);
        emit_lit(b, ")tmp_; }\n");
        return;
    }
    const char shape = element_read_shape(ty);
    emit_lit(b, "        ");
    if (shape == 'p') {
        emit_lit(b, "ZSERIO_TRY("); emit_lit(b, builtin_read_fn(ty->u.type_builtin.code));
        emit_lit(b, "(bs, &"); emit_lit(b, slot); emit_lit(b, "));\n");
    }
    else if (shape == 'o') {
        emit_lit(b, "ZSERIO_TRY("); emit_lit(b, builtin_read_fn(ty->u.type_builtin.code));
        emit_lit(b, "(bs, alloc, &"); emit_lit(b, slot); emit_lit(b, "));\n");
    }
    else if (shape == 'c') {
        const zsp_ast_node_t *decl = peel_alias(ty->resolved);
        (void)emit_type_id(b, decl, src);
        emit_lit(b, "_init(&"); emit_lit(b, slot); emit_lit(b, ");\n");
        emit_lit(b, "        ZSERIO_TRY("); (void)emit_type_id(b, decl, src);
        emit_lit(b, "_read(&"); emit_lit(b, slot); emit_lit(b, ", bs, alloc));\n");
    }
    else if (shape == 'e') {
        const zsp_builtin_code_t uc = enum_underlying(ty);
        const uint32_t           uw = enum_underlying_width(ty);
        if ((uc == ZSP_TB_BIT_FIELD) || (uc == ZSP_TB_INT_FIELD)) {
            const uint32_t w = (uw > 0U) ? uw : 64U;
            emit_lit(b, "{ uint64_t tmp_; ");
            emit_lit(b, "ZSERIO_TRY(zserio_bitstream_read_bits(bs, ");
            emit_uint(b, w);
            emit_lit(b, ", &tmp_)); ");
        }
        else {
            emit_lit(b, "{ "); emit_lit(b, builtin_c_type(uc)); emit_lit(b, " tmp_; ");
            emit_lit(b, "ZSERIO_TRY("); emit_lit(b, builtin_read_fn(uc));
            emit_lit(b, "(bs, &tmp_)); ");
        }
        emit_lit(b, slot);
        emit_lit(b, " = ("); emit_element_type(b, ty, src); emit_lit(b, ")tmp_; }\n");
    }
    else {
        emit_lit(b, "/* unsupported choice-case read */\n");
    }
}

static void emit_choice_case_write(buf_t *b, const zsp_ast_node_t *f,
                                   const zsp_source_t *src)
{
    if (f == NULL) return;
    const zsp_ast_node_t *ty = f->u.field.type;
    if (ty == NULL) return;
    /* Peel subtype aliases — mirror the read path. */
    while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
            && (ty->resolved != NULL)
            && (ty->resolved->kind == ZSP_AST_SUBTYPE)
            && (ty->resolved->u.subtype.target_type != NULL)) {
        ty = ty->resolved->u.subtype.target_type;
    }
    const uint8_t *fn = NULL;
    size_t         fl = 0U;
    if (!ident_span(src, f->u.field.name, &fn, &fl)) return;
    /* Array case: emit length-prefix + per-element write. */
    if (ty->kind == ZSP_AST_TYPE_ARRAY) {
        const zsp_ast_node_t *const lenexpr = ty->u.type_array.length_expr;
        const zsp_ast_node_t *element_ty = ty->u.type_array.element;
        char arr_slot[256];
        size_t aspos = 0U;
        (void)memcpy(arr_slot, "self->u.", 8U); aspos = 8U;
        aspos = snake_into(arr_slot, aspos, fn, fl);
        const char *suf = "[i_]";
        (void)memcpy(&arr_slot[aspos], suf, strlen(suf));
        aspos += strlen(suf);
        arr_slot[aspos] = '\0';
        emit_lit(b, "        {\n");
        if (lenexpr == NULL) {
            emit_lit(b, "        ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_count));\n");
        }
        emit_lit(b, "        const size_t arr_count_ = self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_count;\n");
        emit_lit(b, "        for (size_t i_ = 0u; i_ < arr_count_; ++i_) {\n");
        emit_element_write(b, element_ty, arr_slot, src);
        emit_lit(b, "        }\n");
        emit_lit(b, "        }\n");
        return;
    }
    char slot[256];
    size_t pos = 0U;
    const char *prefix = "self->u.";
    const size_t plen = strlen(prefix);
    (void)memcpy(slot, prefix, plen); pos = plen;
    pos = snake_into(slot, pos, fn, fl);
    slot[pos] = '\0';
    emit_lit(b, "        ");
    emit_element_write(b, ty, slot, src);
}

static void emit_choice_case_free(buf_t *b, const zsp_ast_node_t *f,
                                  const zsp_source_t *src)
{
    if (f == NULL) return;
    const zsp_ast_node_t *ty = f->u.field.type;
    if (ty == NULL) return;
    const uint8_t *fn = NULL;
    size_t         fl = 0U;
    if (!ident_span(src, f->u.field.name, &fn, &fl)) return;

    if (ty->kind == ZSP_AST_TYPE_ARRAY) {
        const zsp_ast_node_t *element_ty = ty->u.type_array.element;
        /* Per-element free for owned scalar element types. */
        bool elem_owned = false;
        if (element_ty != NULL) {
            if (element_ty->kind == ZSP_AST_TYPE_BUILTIN) {
                elem_owned = builtin_needs_free(element_ty->u.type_builtin.code);
            }
            else if (element_ty->kind == ZSP_AST_TYPE_REF) {
                const zsp_ast_node_t *decl = peel_alias(element_ty->resolved);
                if ((decl != NULL)
                        && ((decl->kind == ZSP_AST_STRUCT)
                            || (decl->kind == ZSP_AST_UNION)
                            || (decl->kind == ZSP_AST_CHOICE))) {
                    elem_owned = true;
                }
            }
        }
        if (elem_owned) {
            emit_lit(b, "        for (size_t i_ = 0u; i_ < self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "_count; ++i_) {\n");
            char arr_slot[256];
            size_t aspos = 0U;
            (void)memcpy(arr_slot, "self->u.", 8U); aspos = 8U;
            aspos = snake_into(arr_slot, aspos, fn, fl);
            const char *suf = "[i_]";
            (void)memcpy(&arr_slot[aspos], suf, strlen(suf));
            aspos += strlen(suf);
            arr_slot[aspos] = '\0';
            if (element_ty->kind == ZSP_AST_TYPE_BUILTIN) {
                emit_lit(b, "            ");
                emit_lit(b, builtin_free_fn(element_ty->u.type_builtin.code));
                emit_lit(b, "(&");
                emit_lit(b, arr_slot);
                emit_lit(b, ", alloc);\n");
            }
            else {
                const zsp_ast_node_t *decl = peel_alias(element_ty->resolved);
                emit_lit(b, "            ");
                (void)emit_type_id(b, decl, src);
                emit_lit(b, "_free(&");
                emit_lit(b, arr_slot);
                emit_lit(b, ", alloc);\n");
            }
            emit_lit(b, "        }\n");
        }
        emit_lit(b, "        if (self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, " != NULL) {\n");
        emit_lit(b, "            alloc->vtable->free(alloc->userdata, self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, ", self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_count * sizeof(*self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, "), sizeof(*self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, "));\n");
        emit_lit(b, "            self->u.");
        emit_snake(b, fn, fl);
        emit_lit(b, " = NULL;\n");
        emit_lit(b, "        }\n");
        return;
    }
    if (ty->kind == ZSP_AST_TYPE_BUILTIN) {
        const zsp_builtin_code_t c = ty->u.type_builtin.code;
        if (builtin_needs_free(c)) {
            emit_lit(b, "        ");
            emit_lit(b, builtin_free_fn(c));
            emit_lit(b, "(&self->u.");
            emit_snake(b, fn, fl);
            emit_lit(b, ", alloc);\n");
        }
    }
    else if (ty->kind == ZSP_AST_TYPE_REF) {
        const zsp_ast_node_t *decl = peel_alias(ty->resolved);
        if ((decl != NULL)
                && ((decl->kind == ZSP_AST_STRUCT) || (decl->kind == ZSP_AST_UNION)
                    || (decl->kind == ZSP_AST_CHOICE))) {
            emit_lit(b, "        ");
            (void)emit_type_id(b, decl, src);
            emit_lit(b, "_free(&self->u.");
            emit_snake(b, fn, fl);
            emit_lit(b, ", alloc);\n");
        }
    }
}

static void emit_choice_source(buf_t *b, const zsp_ast_node_t *ch,
                               const zsp_source_t *src)
{
    const uint8_t *name = NULL;
    size_t         nlen = 0U;
    (void)ident_span(src, ch->u.choice.name, &name, &nlen);

    emit_lit(b, "/* Auto-generated by zserio-c parser. */\n");
    emit_lit(b, "#include <string.h>\n");
    emit_lit(b, "#include \"");
    emit_snake(b, name, nlen);
    emit_lit(b, ".h\"\n\n");

    /* init */
    emit_lit(b, "void "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_init("); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_t *self)\n{\n    (void)memset(self, 0, sizeof(*self));\n}\n\n");

    /* set_parameters */
    if (ch->u.choice.n_params > 0U) {
        emit_lit(b, "void "); (void)emit_type_id(b, ch, src);
        emit_lit(b, "_set_parameters("); (void)emit_type_id(b, ch, src);
        emit_lit(b, "_t *self");
        for (size_t i = 0U; i < ch->u.choice.n_params; ++i) {
            const zsp_ast_node_t *p = ch->u.choice.params[i];
            emit_lit(b, ", ");
            bool owned = false;
            (void)emit_field_c_type(b, p->u.parameter.type, src, &owned);
            emit_lit(b, " ");
            const uint8_t *pn = NULL;
            size_t         pl = 0U;
            if (ident_span(src, p->u.parameter.name, &pn, &pl)) {
                emit_snake(b, pn, pl);
            }
        }
        emit_lit(b, ")\n{\n");
        for (size_t i = 0U; i < ch->u.choice.n_params; ++i) {
            const zsp_ast_node_t *p = ch->u.choice.params[i];
            const uint8_t *pn = NULL;
            size_t         pl = 0U;
            if (!ident_span(src, p->u.parameter.name, &pn, &pl)) continue;
            emit_lit(b, "    self->");
            emit_snake(b, pn, pl);
            emit_lit(b, " = ");
            emit_snake(b, pn, pl);
            emit_lit(b, ";\n");
        }
        emit_lit(b, "}\n\n");
    }

    /* read */
    emit_lit(b, "zserio_status_t "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_read("); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_t *self, zserio_bitstream_reader_t *bs,\n"
                "        const zserio_allocator_t *alloc)\n{\n");
    emit_lit(b, "    (void)self;\n");
    emit_lit(b, "    (void)bs;\n");
    emit_lit(b, "    (void)alloc;\n");
    emit_lit(b, "    switch ((int)(");
    emit_expr_c(b, ch->u.choice.selector, src);
    emit_lit(b, ")) {\n");
    /* Schemas can spell `case A: case B: ... field;` — the
     * parser stores each `case A:` / `case B:` as a separate
     * CHOICE_CASE with NULL field, with only the last carrying
     * the field. We collapse those by skipping body+break when
     * the field is NULL and the immediate next case has either
     * its own labels but no field, or a body field. Empty cases
     * (lone `;` body) emit a real break, since they're terminal. */
    for (size_t i = 0U; i < ch->u.choice.n_cases; ++i) {
        const zsp_ast_node_t *c = ch->u.choice.cases[i];
        for (size_t j = 0U; j < c->u.choice_case.n_labels; ++j) {
            emit_lit(b, "    case ");
            emit_expr_c(b, c->u.choice_case.labels[j], src);
            emit_lit(b, ":\n");
        }
        const bool fallthrough_to_next =
                (c->u.choice_case.field == NULL)
                && !c->u.choice_case.has_body
                && ((i + 1U) < ch->u.choice.n_cases)
                && (ch->u.choice.cases[i + 1U]->u.choice_case.n_labels > 0U)
                && (ch->u.choice.cases[i + 1U]->u.choice_case.labels[0]->kind
                        != ZSP_AST_CHOICE_DEFAULT);
        if (fallthrough_to_next) {
            continue;
        }
        emit_choice_case_read(b, c->u.choice_case.field, src);
        emit_lit(b, "        break;\n");
    }
    emit_lit(b, "    default:\n");
    if (ch->u.choice.default_case != NULL) {
        emit_choice_case_read(b, ch->u.choice.default_case->u.choice_default.field, src);
    }
    emit_lit(b, "        break;\n");
    emit_lit(b, "    }\n    return ZSERIO_OK;\n}\n\n");

    /* write */
    emit_lit(b, "zserio_status_t "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_write(const "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_t *self, zserio_bitstream_writer_t *bs)\n{\n");
    emit_lit(b, "    (void)self;\n    (void)bs;\n");
    emit_lit(b, "    switch ((int)(");
    emit_expr_c(b, ch->u.choice.selector, src);
    emit_lit(b, ")) {\n");
    for (size_t i = 0U; i < ch->u.choice.n_cases; ++i) {
        const zsp_ast_node_t *c = ch->u.choice.cases[i];
        for (size_t j = 0U; j < c->u.choice_case.n_labels; ++j) {
            emit_lit(b, "    case ");
            emit_expr_c(b, c->u.choice_case.labels[j], src);
            emit_lit(b, ":\n");
        }
        const bool fallthrough_to_next =
                (c->u.choice_case.field == NULL)
                && !c->u.choice_case.has_body
                && ((i + 1U) < ch->u.choice.n_cases)
                && (ch->u.choice.cases[i + 1U]->u.choice_case.n_labels > 0U);
        if (fallthrough_to_next) continue;
        emit_choice_case_write(b, c->u.choice_case.field, src);
        emit_lit(b, "        break;\n");
    }
    emit_lit(b, "    default:\n");
    if (ch->u.choice.default_case != NULL) {
        emit_choice_case_write(b, ch->u.choice.default_case->u.choice_default.field, src);
    }
    emit_lit(b, "        break;\n");
    emit_lit(b, "    }\n    return ZSERIO_OK;\n}\n\n");

    /* free */
    emit_lit(b, "void "); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_free("); (void)emit_type_id(b, ch, src);
    emit_lit(b, "_t *self, const zserio_allocator_t *alloc)\n{\n");
    emit_lit(b, "    (void)self;\n    (void)alloc;\n");
    emit_lit(b, "    switch ((int)(");
    emit_expr_c(b, ch->u.choice.selector, src);
    emit_lit(b, ")) {\n");
    for (size_t i = 0U; i < ch->u.choice.n_cases; ++i) {
        const zsp_ast_node_t *c = ch->u.choice.cases[i];
        for (size_t j = 0U; j < c->u.choice_case.n_labels; ++j) {
            emit_lit(b, "    case ");
            emit_expr_c(b, c->u.choice_case.labels[j], src);
            emit_lit(b, ":\n");
        }
        const bool fallthrough_to_next =
                (c->u.choice_case.field == NULL)
                && !c->u.choice_case.has_body
                && ((i + 1U) < ch->u.choice.n_cases)
                && (ch->u.choice.cases[i + 1U]->u.choice_case.n_labels > 0U);
        if (fallthrough_to_next) continue;
        emit_choice_case_free(b, c->u.choice_case.field, src);
        emit_lit(b, "        break;\n");
    }
    emit_lit(b, "    default:\n");
    if (ch->u.choice.default_case != NULL) {
        emit_choice_case_free(b, ch->u.choice.default_case->u.choice_default.field, src);
    }
    emit_lit(b, "        break;\n");
    emit_lit(b, "    }\n}\n");
}

static void emit_choice_header(buf_t *b, const zsp_ast_node_t *ch,
                               const zsp_source_t *src)
{
    emit_lit(b, "/* Auto-generated by zserio-c parser. */\n");
    emit_lit(b, "#ifndef ZSP_GENERATED_");
    (void)emit_type_id(b, ch, src);
    emit_lit(b, "_H_\n");
    emit_lit(b, "#define ZSP_GENERATED_");
    (void)emit_type_id(b, ch, src);
    emit_lit(b, "_H_\n\n");
    emit_lit(b, "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n");
    emit_lit(b, "#include <string.h>\n\n");
    emit_lit(b, "#include \"zserio/allocator.h\"\n#include \"zserio/bitstream.h\"\n");
    emit_lit(b, "#include \"zserio/bytes.h\"\n#include \"zserio/extern.h\"\n");
    emit_lit(b, "#include \"zserio/float.h\"\n#include \"zserio/primitives.h\"\n");
    emit_lit(b, "#include \"zserio/status.h\"\n#include \"zserio/string.h\"\n");
    emit_lit(b, "#include \"zserio/varint.h\"\n\n");
    /* Collect referenced types from parameters, union members,
     * and default field. Same dedup approach as for compounds.
     * Qualify cross-package includes with the relative `../<pkg>/`
     * path so the emitter output compiles. */
    {
        enum { MAX_INC = 512U };
        const uint8_t *names[MAX_INC];
        size_t          lens[MAX_INC];
        const zsp_ast_node_t *decls_for_pkg[MAX_INC];
        size_t          n = 0U;

        char self_pkg[1024];
        const size_t self_len = decl_package_path(ch, src, self_pkg, sizeof(self_pkg));

        const zsp_ast_node_t *referenced_types[1024];
        size_t n_refs = 0U;
        for (size_t i = 0U; (i < ch->u.choice.n_params) && (n_refs < 1024U); ++i) {
            referenced_types[n_refs++] = ch->u.choice.params[i]->u.parameter.type;
        }
        for (size_t i = 0U; (i < ch->u.choice.n_cases) && (n_refs < 1024U); ++i) {
            const zsp_ast_node_t *f = ch->u.choice.cases[i]->u.choice_case.field;
            if (f != NULL) {
                referenced_types[n_refs++] = f->u.field.type;
            }
        }
        if ((ch->u.choice.default_case != NULL)
                && (ch->u.choice.default_case->u.choice_default.field != NULL)
                && (n_refs < 1024U)) {
            referenced_types[n_refs++] =
                ch->u.choice.default_case->u.choice_default.field->u.field.type;
        }
        for (size_t r = 0U; r < n_refs; ++r) {
            const zsp_ast_node_t *ty = referenced_types[r];
            if (ty == NULL) continue;
            if (ty->kind == ZSP_AST_TYPE_ARRAY) {
                ty = ty->u.type_array.element;
            }
            if ((ty == NULL) || (ty->kind != ZSP_AST_TYPE_REF)) continue;
            const zsp_ast_node_t *decl = ty->resolved;
            if (decl == NULL) continue;
            const zsp_ast_node_t *name_node = NULL;
            if ((decl->kind == ZSP_AST_STRUCT) || (decl->kind == ZSP_AST_UNION)) {
                name_node = decl->u.compound.name;
            }
            else if (decl->kind == ZSP_AST_CHOICE)  { name_node = decl->u.choice.name; }
            else if (decl->kind == ZSP_AST_ENUM)    { name_node = decl->u.enum_decl.name; }
            else if (decl->kind == ZSP_AST_BITMASK) { name_node = decl->u.bitmask_decl.name; }
            else if (decl->kind == ZSP_AST_SUBTYPE) { name_node = decl->u.subtype.alias_name; }
            const uint8_t *nb = NULL;
            size_t         nl = 0U;
            if (!ident_span(src, name_node, &nb, &nl)) continue;
            bool seen = false;
            for (size_t k = 0U; k < n; ++k) {
                if ((lens[k] == nl) && (memcmp(names[k], nb, nl) == 0)) {
                    seen = true;
                    break;
                }
            }
            if (seen || (n >= MAX_INC)) continue;
            names[n] = nb;
            lens[n]  = nl;
            decls_for_pkg[n] = decl;
            n += 1U;
        }
        size_t levels_up = 0U;
        if (self_len > 0U) {
            levels_up = 1U;
            for (size_t k = 0U; k < self_len; ++k) {
                if (self_pkg[k] == '/') levels_up += 1U;
            }
        }
        for (size_t i = 0U; i < n; ++i) {
            char dep_pkg[1024];
            const size_t dep_len = decl_package_path(
                    decls_for_pkg[i], src, dep_pkg, sizeof(dep_pkg));
            const bool same_pkg = ((self_len == dep_len)
                    && (memcmp(self_pkg, dep_pkg, dep_len) == 0));
            emit_lit(b, "#include \"");
            if (!same_pkg) {
                for (size_t k = 0U; k < levels_up; ++k) {
                    emit_lit(b, "../");
                }
                if (dep_len > 0U) {
                    emit_bytes(b, dep_pkg, dep_len);
                    emit_lit(b, "/");
                }
            }
            emit_snake(b, names[i], lens[i]);
            emit_lit(b, ".h\"\n");
        }
        /* Scan case labels + selector expression for IDENT/MEMBER
         * references that resolve to top-level CONST decls and
         * emit their headers. Without this, choices that use a
         * const value in a `case` label don't compile. */
        const zsp_ast_node_t *const_decls[32];
        size_t n_const_decls = 0U;
        const zsp_ast_node_t *expr_roots[1 + 16];
        size_t n_expr_roots = 0U;
        if (ch->u.choice.selector != NULL) {
            expr_roots[n_expr_roots++] = ch->u.choice.selector;
        }
        for (size_t i = 0U; i < ch->u.choice.n_cases; ++i) {
            const zsp_ast_node_t *c = ch->u.choice.cases[i];
            for (size_t j = 0U; (j < c->u.choice_case.n_labels)
                    && (n_expr_roots < sizeof(expr_roots)/sizeof(expr_roots[0])); ++j) {
                expr_roots[n_expr_roots++] = c->u.choice_case.labels[j];
            }
        }
        for (size_t e = 0U; e < n_expr_roots; ++e) {
            const zsp_ast_node_t *stack[64];
            size_t                sp = 0U;
            if (expr_roots[e] != NULL) stack[sp++] = expr_roots[e];
            while ((sp > 0U) && (n_const_decls < 32U)) {
                const zsp_ast_node_t *x = stack[--sp];
                if (x == NULL) continue;
                const zsp_ast_node_t *d = x->resolved;
                if ((d != NULL) && (d->kind == ZSP_AST_CONST)) {
                    bool seen = false;
                    for (size_t k = 0U; k < n_const_decls; ++k) {
                        if (const_decls[k] == d) { seen = true; break; }
                    }
                    if (!seen) const_decls[n_const_decls++] = d;
                }
                if (x->kind == ZSP_AST_EXPR_BINARY) {
                    if ((sp + 1U) < 64U) { stack[sp++] = x->u.binop.lhs; stack[sp++] = x->u.binop.rhs; }
                }
                else if (x->kind == ZSP_AST_EXPR_UNARY) {
                    if (sp < 64U) stack[sp++] = x->u.unop.operand;
                }
                else if (x->kind == ZSP_AST_EXPR_TERNARY) {
                    if ((sp + 2U) < 64U) {
                        stack[sp++] = x->u.ternary.cond;
                        stack[sp++] = x->u.ternary.then_expr;
                        stack[sp++] = x->u.ternary.else_expr;
                    }
                }
                else if (x->kind == ZSP_AST_EXPR_MEMBER) {
                    if (sp < 64U) stack[sp++] = x->u.member.lhs;
                }
                else if (x->kind == ZSP_AST_EXPR_INDEX) {
                    if ((sp + 1U) < 64U) {
                        stack[sp++] = x->u.index.lhs;
                        stack[sp++] = x->u.index.index;
                    }
                }
                else if (x->kind == ZSP_AST_EXPR_CALL) {
                    for (size_t a = 0U; (a < x->u.call.n_args) && (sp < 64U); ++a) {
                        stack[sp++] = x->u.call.args[a];
                    }
                }
            }
        }
        for (size_t i = 0U; i < n_const_decls; ++i) {
            const zsp_ast_node_t *cn = const_decls[i]->u.const_decl.name;
            const uint8_t *cb = NULL;
            size_t         cl = 0U;
            if (!ident_span(src, cn, &cb, &cl)) continue;
            char dep_pkg[1024];
            const size_t dep_len = decl_package_path(
                    const_decls[i], src, dep_pkg, sizeof(dep_pkg));
            const bool same_pkg = ((self_len == dep_len)
                    && (memcmp(self_pkg, dep_pkg, dep_len) == 0));
            emit_lit(b, "#include \"");
            if (!same_pkg) {
                for (size_t k = 0U; k < levels_up; ++k) {
                    emit_lit(b, "../");
                }
                if (dep_len > 0U) {
                    emit_bytes(b, dep_pkg, dep_len);
                    emit_lit(b, "/");
                }
            }
            emit_snake(b, cb, cl);
            emit_lit(b, ".h\"\n");
        }
        if ((n > 0U) || (n_const_decls > 0U)) {
            emit_lit(b, "\n");
        }
    }

    /* Template-param placeholders for choices (same as compounds). */
    for (size_t i = 0U; i < ch->u.choice.n_template_params; ++i) {
        const zsp_ast_node_t *tp = ch->u.choice.template_params[i];
        const uint8_t *tpn = NULL;
        size_t         tpl = 0U;
        if (ident_span(src, tp->u.template_param.name, &tpn, &tpl)) {
            /* Placeholder typedef so the generated C compiles
             * before full template instantiation lands. int64_t
             * is switch-compatible (used as choice selector) and
             * representation-equivalent to any zserio scalar; for
             * compound template args the generated code may need
             * additional template-instantiation work to be wire-
             * correct, but it compiles. */
            emit_lit(b, "typedef int64_t ");
            emit_snake(b, tpn, tpl);
            emit_lit(b, "_t;  /* template-param placeholder */\n");
        }
    }
    if (ch->u.choice.n_template_params > 0U) {
        emit_lit(b, "\n");
    }

    emit_choice_decl(b, ch, src);
    emit_choice_protos(b, ch, src);
    emit_lit(b, "\n#endif\n");
}

/* ----- Enum + bitmask emission ------------------------------ */

static void emit_enum_header(buf_t *b, const zsp_ast_node_t *enm,
                             const zsp_source_t *src)
{
    emit_lit(b, "/* Auto-generated by zserio-c parser. Do not edit. */\n");
    emit_lit(b, "#ifndef ZSP_GENERATED_");
    (void)emit_type_id(b, enm, src);
    emit_lit(b, "_H_\n");
    emit_lit(b, "#define ZSP_GENERATED_");
    (void)emit_type_id(b, enm, src);
    emit_lit(b, "_H_\n\n");
    emit_lit(b, "#include <stdint.h>\n\n");

    emit_lit(b, "typedef enum ");
    (void)emit_type_id(b, enm, src);
    emit_lit(b, " {\n");
    const uint8_t *enm_name = NULL;
    size_t         enm_name_len = 0U;
    (void)ident_span(src, enm->u.enum_decl.name, &enm_name, &enm_name_len);
    for (size_t i = 0U; i < enm->u.enum_decl.n_items; ++i) {
        const zsp_ast_node_t *item = enm->u.enum_decl.items[i];
        emit_lit(b, "    ");
        emit_pkg_prefix(b, enm, src);
        emit_upper_snake(b, enm_name, enm_name_len);
        emit_lit(b, "_");
        const uint8_t *in = NULL;
        size_t         il = 0U;
        if (ident_span(src, item->u.enum_item.name, &in, &il)) {
            emit_span(b, in, il);
        }
        emit_lit(b, " = ");
        const zsp_sema_const_t *const v = (const zsp_sema_const_t *)item->computed;
        if (v != NULL) {
            emit_uint(b, v->v.u);
        }
        else {
            emit_uint(b, (uint64_t)i);
        }
        if (i + 1U < enm->u.enum_decl.n_items) {
            emit_lit(b, ",\n");
        }
        else {
            emit_lit(b, "\n");
        }
    }
    emit_lit(b, "} ");
    (void)emit_type_id(b, enm, src);
    emit_lit(b, "_t;\n\n");
    emit_lit(b, "#endif\n");
}

/* ----- Path construction ------------------------------------ */

/* Build "<dir>/<name>.h" or ".c" path. */
static void build_path(char *out, size_t out_cap,
                       const uint8_t *dir, size_t dir_len,
                       const uint8_t *name, size_t name_len,
                       const char *ext)
{
    /* Copy dir lowercased + replace '.' with '/'. */
    size_t pos = 0U;
    for (size_t i = 0U; (i < dir_len) && (pos + 2U < out_cap); ++i) {
        const uint8_t c = dir[i];
        if (c == (uint8_t)'.') {
            out[pos++] = '/';
        }
        else if ((c >= (uint8_t)'A') && (c <= (uint8_t)'Z')) {
            out[pos++] = (char)(c - (uint8_t)'A' + (uint8_t)'a');
        }
        else {
            out[pos++] = (char)c;
        }
    }
    if (dir_len > 0U) {
        out[pos++] = '/';
    }
    /* Append snake-cased name using the shared look-ahead rule. */
    if (pos + 2U * name_len < out_cap) {
        pos = snake_into(out, pos, name, name_len);
    }
    /* Append extension. */
    const size_t elen = strlen(ext);
    for (size_t i = 0U; (i < elen) && (pos + 1U < out_cap); ++i) {
        out[pos++] = ext[i];
    }
    out[pos] = '\0';
}

/* Compute the package name span on the dotted-name node. */
static void package_span(const emit_ctx_t *ctx, char *out, size_t out_cap)
{
    out[0] = '\0';
    if (ctx->unit->u.unit.package == NULL) {
        return;
    }
    const zsp_ast_node_t *pname = ctx->unit->u.unit.package->u.package.name;
    if (pname == NULL) {
        return;
    }
    if (pname->kind == ZSP_AST_EXPR_IDENT) {
        const uint8_t *bytes = NULL;
        size_t         len   = 0U;
        if (ident_span(ctx->source, pname, &bytes, &len)) {
            size_t copy = (len < (out_cap - 1U)) ? len : (out_cap - 1U);
            (void)memcpy(out, bytes, copy);
            out[copy] = '\0';
        }
    }
    else if (pname->kind == ZSP_AST_DOTTED_NAME) {
        size_t pos = 0U;
        for (size_t i = 0U; i < pname->u.dotted.n_parts; ++i) {
            const uint8_t *bytes = NULL;
            size_t         len   = 0U;
            if (ident_span(ctx->source, pname->u.dotted.parts[i], &bytes, &len)) {
                if ((i > 0U) && ((pos + 1U) < out_cap)) {
                    out[pos++] = '.';
                }
                const size_t copy = (len < (out_cap - 1U - pos)) ? len : (out_cap - 1U - pos);
                (void)memcpy(&out[pos], bytes, copy);
                pos += copy;
            }
        }
        out[pos] = '\0';
    }
}

/* ----- Main emit entry --------------------------------------- */

zserio_status_t zsp_emit_unit(const zsp_ast_node_t *unit,
                              zsp_arena_t          *arena,
                              zsp_diag_collector_t *diag,
                              const zsp_source_t   *source,
                              zsp_emit_write_fn     write,
                              void                 *userdata)
{
    if ((unit == NULL) || (arena == NULL) || (diag == NULL)
            || (source == NULL) || (write == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (unit->kind != ZSP_AST_UNIT) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    emit_ctx_t ctx;
    ctx.unit          = unit;
    ctx.arena         = arena;
    ctx.diag          = diag;
    ctx.source        = source;
    ctx.write         = write;
    ctx.write_userdata = userdata;
    ctx.pkg_name      = NULL;
    ctx.pkg_name_len  = 0U;

    char pkg[256];
    package_span(&ctx, pkg, sizeof(pkg));
    const size_t pkg_len = strlen(pkg);

    for (size_t i = 0U; i < unit->u.unit.n_decls; ++i) {
        const zsp_ast_node_t *const d = unit->u.unit.decls[i];
        if (d == NULL) continue;

        char path_h[512];
        char path_c[512];
        const uint8_t *name = NULL;
        size_t         nlen = 0U;
        const zsp_ast_node_t *name_node = NULL;

        /* Skip templated decls: their concrete instantiations are
         * emitted separately by the template-instantiation pass.
         * Emitting the template itself produces code referencing
         * unbounded T-placeholders that won't compile when the
         * field types pull in real cross-package decls. */
        {
            const size_t n_tp =
                    ((d->kind == ZSP_AST_STRUCT) || (d->kind == ZSP_AST_UNION))
                    ? d->u.compound.n_template_params
                    : (d->kind == ZSP_AST_CHOICE)
                    ? d->u.choice.n_template_params
                    : (d->kind == ZSP_AST_SQL_TABLE)
                    ? d->u.sql_table.n_template_params
                    : 0U;
            if (n_tp > 0U) {
                continue;
            }
        }
        if ((d->kind == ZSP_AST_STRUCT) || (d->kind == ZSP_AST_UNION)) {
            name_node = d->u.compound.name;
        }
        else if (d->kind == ZSP_AST_ENUM) {
            name_node = d->u.enum_decl.name;
        }
        else if (d->kind == ZSP_AST_BITMASK) {
            name_node = d->u.bitmask_decl.name;
        }
        else if (d->kind == ZSP_AST_CONST) {
            name_node = d->u.const_decl.name;
        }
        else if (d->kind == ZSP_AST_SUBTYPE) {
            name_node = d->u.subtype.alias_name;
        }
        else if (d->kind == ZSP_AST_CHOICE) {
            name_node = d->u.choice.name;
        }
        else if (d->kind == ZSP_AST_SQL_TABLE) {
            name_node = d->u.sql_table.name;
        }
        else if (d->kind == ZSP_AST_SQL_DATABASE) {
            name_node = d->u.sql_database.name;
        }
        else if (d->kind == ZSP_AST_SERVICE) {
            name_node = d->u.service.name;
        }
        else if (d->kind == ZSP_AST_PUBSUB) {
            name_node = d->u.pubsub.name;
        }
        else if (d->kind == ZSP_AST_RULE_GROUP) {
            name_node = d->u.rule_group.name;
        }
        else if (d->kind == ZSP_AST_INSTANTIATE) {
            name_node = d->u.instantiate.name;
        }
        else {
            /* Skip declarations the MVP emitter doesn't handle yet. */
            (void)zsp_diag_emit_lit(diag, ZSP_SEV_WARN,
                                    d->file_id, d->offset,
                                    "declaration kind not yet emitted; skipping");
            continue;
        }

        if (!ident_span(source, name_node, &name, &nlen)) {
            continue;
        }
        build_path(path_h, sizeof(path_h),
                   (const uint8_t *)pkg, pkg_len, name, nlen, ".h");
        build_path(path_c, sizeof(path_c),
                   (const uint8_t *)pkg, pkg_len, name, nlen, ".c");

        buf_t hbuf;
        buf_t cbuf;
        buf_init(&hbuf, arena);
        buf_init(&cbuf, arena);

        if (d->kind == ZSP_AST_CHOICE) {
            emit_choice_header(&hbuf, d, source);
            emit_choice_source(&cbuf, d, source);
            if (hbuf.oom || cbuf.oom) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            if (!write(userdata, path_c, cbuf.bytes, cbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        if ((d->kind == ZSP_AST_STRUCT) || (d->kind == ZSP_AST_UNION)) {
            emit_header_for_compound(&hbuf, d, source);
            emit_source_for_compound(&cbuf, d, source);
            if (hbuf.oom || cbuf.oom) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            /* Include `<string.h>` in the .c to provide memset */
            buf_t cbuf2;
            buf_init(&cbuf2, arena);
            emit_lit(&cbuf2, "/* Auto-generated by zserio-c parser. Do not edit. */\n");
            emit_lit(&cbuf2, "#include <string.h>\n");
            emit_lit(&cbuf2, "#include \"");
            emit_snake(&cbuf2, name, nlen);
            emit_lit(&cbuf2, ".h\"\n\n");
            /* Append the body sans its own header comment + include. */
            /* Quick approach: rebuild without the prefix line. The
             * source emitter already includes its own header; for
             * simplicity we just keep cbuf as-is. */
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            if (!write(userdata, path_c, cbuf.bytes, cbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
        }
        else if (d->kind == ZSP_AST_ENUM) {
            emit_enum_header(&hbuf, d, source);
            if (hbuf.oom) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
        }
        else if (d->kind == ZSP_AST_BITMASK) {
            /* Bitmask: emit a typedef'd integer plus #defines for
             * each flag. Mirror the enum shape since the analyser
             * already evaluated each item's value. */
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser. */\n");
            emit_lit(&hbuf, "#ifndef ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n");
            emit_lit(&hbuf, "#define ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n\n");
            emit_lit(&hbuf, "#include <stdint.h>\n\n");
            const zsp_ast_node_t *const base = d->u.bitmask_decl.base_type;
            const char *underlying = "uint32_t";
            if ((base != NULL) && (base->kind == ZSP_AST_TYPE_BUILTIN)) {
                underlying = builtin_c_type(base->u.type_builtin.code);
            }
            emit_lit(&hbuf, "typedef ");
            emit_lit(&hbuf, underlying);
            emit_lit(&hbuf, " ");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_t;\n");
            for (size_t k = 0U; k < d->u.bitmask_decl.n_items; ++k) {
                const zsp_ast_node_t *item = d->u.bitmask_decl.items[k];
                emit_lit(&hbuf, "#define ");
                emit_pkg_prefix(&hbuf, d, source);
                emit_upper_snake(&hbuf, name, nlen);
                emit_lit(&hbuf, "_");
                const uint8_t *in = NULL;
                size_t         il = 0U;
                if (ident_span(source, item->u.bitmask_item.name, &in, &il)) {
                    emit_span(&hbuf, in, il);
                }
                emit_lit(&hbuf, " ((");
                (void)emit_type_id(&hbuf, d, source);
                emit_lit(&hbuf, "_t)");
                const zsp_sema_const_t *const v =
                        (const zsp_sema_const_t *)item->computed;
                if (v != NULL) {
                    emit_uint(&hbuf, v->v.u);
                }
                else {
                    emit_uint(&hbuf, (uint64_t)(((uint64_t)1U) << k));
                }
                emit_lit(&hbuf, "U)\n");
            }
            emit_lit(&hbuf, "\n#endif\n");
            if (hbuf.oom) return ZSERIO_ERR_OUT_OF_MEMORY;
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
        }
        else if (d->kind == ZSP_AST_CONST) {
            /* Const decl → `#define <pkg>_<NAME> <value>`. Falls
             * back to a comment if const-eval didn't compute a
             * value. */
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser. */\n");
            emit_lit(&hbuf, "#ifndef ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n");
            emit_lit(&hbuf, "#define ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n\n");
            emit_lit(&hbuf, "#include <stdint.h>\n\n");
            const zsp_ast_node_t *const value_expr =
                    d->u.const_decl.value_expr;
            const zsp_sema_const_t *const cv = (value_expr != NULL)
                    ? (const zsp_sema_const_t *)value_expr->computed
                    : NULL;
            emit_lit(&hbuf, "#define ");
            emit_pkg_prefix(&hbuf, d, source);
            emit_span(&hbuf, name, nlen);
            emit_lit(&hbuf, " (");
            if (cv != NULL) {
                emit_uint(&hbuf, cv->v.u);
                emit_lit(&hbuf, "U");
            }
            else {
                emit_lit(&hbuf, "0U /*const-eval unavailable*/");
            }
            emit_lit(&hbuf, ")\n\n#endif\n");
            if (hbuf.oom) return ZSERIO_ERR_OUT_OF_MEMORY;
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
        }
        else if (d->kind == ZSP_AST_SQL_TABLE) {
            /* SQL table: emit a struct-shaped header with field
             * decls. No read/write — runtime SQL bindings are
             * the integrator's responsibility. */
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser (sql_table). */\n");
            emit_lit(&hbuf, "#ifndef ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n");
            emit_lit(&hbuf, "#define ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n\n");
            emit_lit(&hbuf, "#include <stdbool.h>\n#include <stddef.h>\n");
            emit_lit(&hbuf, "#include <stdint.h>\n");
            emit_lit(&hbuf, "#include \"zserio/string.h\"\n");
            emit_lit(&hbuf, "#include \"zserio/bytes.h\"\n\n");

            emit_lit(&hbuf, "typedef struct ");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, " {\n");
            for (size_t k = 0U; k < d->u.sql_table.n_fields; ++k) {
                const zsp_ast_node_t *sf = d->u.sql_table.fields[k];
                if (sf == NULL) continue;
                emit_lit(&hbuf, "    ");
                bool owned = false;
                (void)emit_field_c_type(&hbuf, sf->u.sql_field.type,
                                        source, &owned);
                emit_lit(&hbuf, " ");
                const uint8_t *sfn = NULL;
                size_t         sfl = 0U;
                if (ident_span(source, sf->u.sql_field.name, &sfn, &sfl)) {
                    emit_snake(&hbuf, sfn, sfl);
                }
                emit_lit(&hbuf, ";\n");
            }
            emit_lit(&hbuf, "} ");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_t;\n\n#endif\n");
            if (hbuf.oom) return ZSERIO_ERR_OUT_OF_MEMORY;
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        else if (d->kind == ZSP_AST_SQL_DATABASE) {
            /* SQL database: emit a struct of table pointers. */
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser (sql_database). */\n");
            emit_lit(&hbuf, "#ifndef ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n");
            emit_lit(&hbuf, "#define ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n\n");
            emit_lit(&hbuf, "#include <stddef.h>\n\n");
            /* Forward-declare included table types. */
            for (size_t k = 0U; k < d->u.sql_database.n_table_refs; ++k) {
                const zsp_ast_node_t *r = d->u.sql_database.table_refs[k];
                const zsp_ast_node_t *ty = r->u.sql_table_ref.type;
                const zsp_ast_node_t *tname = (ty && (ty->kind == ZSP_AST_TYPE_REF))
                        ? ty->u.type_ref.name : NULL;
                if ((tname != NULL) && (tname->kind == ZSP_AST_DOTTED_NAME)) {
                    tname = tname->u.dotted.parts[tname->u.dotted.n_parts - 1U];
                }
                const uint8_t *tn = NULL;
                size_t         tl = 0U;
                if (ident_span(source, tname, &tn, &tl)) {
                    emit_lit(&hbuf, "#include \"");
                    emit_snake(&hbuf, tn, tl);
                    emit_lit(&hbuf, ".h\"\n");
                }
            }
            emit_lit(&hbuf, "\ntypedef struct ");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, " {\n");
            for (size_t k = 0U; k < d->u.sql_database.n_table_refs; ++k) {
                const zsp_ast_node_t *r = d->u.sql_database.table_refs[k];
                bool owned = false;
                emit_lit(&hbuf, "    ");
                (void)emit_field_c_type(&hbuf, r->u.sql_table_ref.type,
                                        source, &owned);
                emit_lit(&hbuf, " *");
                const uint8_t *rn = NULL;
                size_t         rl = 0U;
                if (ident_span(source, r->u.sql_table_ref.name, &rn, &rl)) {
                    emit_snake(&hbuf, rn, rl);
                }
                emit_lit(&hbuf, ";\n");
            }
            emit_lit(&hbuf, "} ");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_t;\n\n#endif\n");
            if (hbuf.oom) return ZSERIO_ERR_OUT_OF_MEMORY;
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        else if (d->kind == ZSP_AST_SERVICE) {
            /* Service: emit method prototypes. Runtime hooks are
             * the integrator's; the generated header is a
             * type-and-prototype declaration only. */
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser (service). */\n");
            emit_lit(&hbuf, "#ifndef ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n");
            emit_lit(&hbuf, "#define ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n\n");
            emit_lit(&hbuf, "#include \"zserio/status.h\"\n\n");
            for (size_t k = 0U; k < d->u.service.n_methods; ++k) {
                const zsp_ast_node_t *m = d->u.service.methods[k];
                emit_lit(&hbuf, "/* ");
                const uint8_t *mn = NULL;
                size_t         ml = 0U;
                if (ident_span(source, m->u.service_method.name, &mn, &ml)) {
                    emit_span(&hbuf, mn, ml);
                }
                emit_lit(&hbuf, ": service method declaration */\n");
            }
            emit_lit(&hbuf, "\n#endif\n");
            if (hbuf.oom) return ZSERIO_ERR_OUT_OF_MEMORY;
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        else if (d->kind == ZSP_AST_PUBSUB) {
            /* Pubsub: emit topic message-shape declarations as a
             * comment summary plus a placeholder typedef so
             * downstream code has something to refer to. */
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser (pubsub). */\n");
            emit_lit(&hbuf, "#ifndef ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n");
            emit_lit(&hbuf, "#define ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n\n");
            emit_lit(&hbuf, "/* topic messages:\n");
            for (size_t k = 0U; k < d->u.pubsub.n_messages; ++k) {
                const zsp_ast_node_t *m = d->u.pubsub.messages[k];
                emit_lit(&hbuf, " *   ");
                if (m->u.pubsub_message.publish)   emit_lit(&hbuf, "publish ");
                if (m->u.pubsub_message.subscribe) emit_lit(&hbuf, "subscribe ");
                const uint8_t *mn = NULL;
                size_t         ml = 0U;
                if (ident_span(source, m->u.pubsub_message.name, &mn, &ml)) {
                    emit_span(&hbuf, mn, ml);
                }
                emit_lit(&hbuf, "\n");
            }
            emit_lit(&hbuf, " */\n");
            emit_lit(&hbuf, "typedef struct ");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_pubsub { int placeholder; } ");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_t;\n\n#endif\n");
            if (hbuf.oom) return ZSERIO_ERR_OUT_OF_MEMORY;
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        else if (d->kind == ZSP_AST_RULE_GROUP) {
            /* Rule groups have no runtime representation —
             * upstream uses them for validation. Emit a marker
             * comment so the file exists in the output tree. */
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser (rule_group). */\n");
            emit_lit(&hbuf, "/* rule group: no runtime emission. */\n");
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        else if (d->kind == ZSP_AST_INSTANTIATE) {
            /* Instantiation alias: forward to the concrete clone
             * produced by the template-instantiation pass. We
             * emit a typedef + a set of #define forwarders for
             * the init/read/write/free entry points so calling
             * code can use either spelling. */
            const zsp_ast_node_t *tgt = d->u.instantiate.target;
            const zsp_ast_node_t *concrete = (tgt != NULL) ? tgt->resolved : NULL;
            /* Peel one level if the resolved is still an INSTANTIATE
             * (chain of aliases). */
            while ((concrete != NULL)
                    && (concrete->kind == ZSP_AST_INSTANTIATE)
                    && (concrete->u.instantiate.target != NULL)) {
                concrete = concrete->u.instantiate.target->resolved;
            }
            const zsp_ast_node_t *concrete_name = NULL;
            if (concrete != NULL) {
                if ((concrete->kind == ZSP_AST_STRUCT)
                        || (concrete->kind == ZSP_AST_UNION)) {
                    concrete_name = concrete->u.compound.name;
                } else if (concrete->kind == ZSP_AST_CHOICE) {
                    concrete_name = concrete->u.choice.name;
                }
            }
            const uint8_t *cn_bytes = NULL;
            size_t         cn_len   = 0U;
            const bool have_concrete = (concrete_name != NULL)
                    && ident_span(source, concrete_name, &cn_bytes, &cn_len);
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser (instantiate). */\n");
            emit_lit(&hbuf, "#ifndef ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n");
            emit_lit(&hbuf, "#define ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n\n");
            if (have_concrete) {
                emit_lit(&hbuf, "#include \"");
                emit_snake(&hbuf, cn_bytes, cn_len);
                emit_lit(&hbuf, ".h\"\n\n");
                emit_lit(&hbuf, "typedef ");
                (void)emit_type_id(&hbuf, concrete, source);
                emit_lit(&hbuf, "_t ");
                (void)emit_type_id(&hbuf, d, source);
                emit_lit(&hbuf, "_t;\n");
                static const char *const fns[] = {
                    "init", "set_parameters", "read", "write", "free"
                };
                for (size_t fi = 0U; fi < sizeof(fns)/sizeof(fns[0]); ++fi) {
                    emit_lit(&hbuf, "#define ");
                    (void)emit_type_id(&hbuf, d, source);
                    emit_lit(&hbuf, "_");
                    emit_lit(&hbuf, fns[fi]);
                    emit_lit(&hbuf, " ");
                    (void)emit_type_id(&hbuf, concrete, source);
                    emit_lit(&hbuf, "_");
                    emit_lit(&hbuf, fns[fi]);
                    emit_lit(&hbuf, "\n");
                }
            } else {
                emit_lit(&hbuf, "/* instantiate: target not resolved. */\n");
            }
            emit_lit(&hbuf, "\n#endif\n");
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        else if (d->kind == ZSP_AST_SUBTYPE) {
            /* Subtype: emit a typedef of the target's C type. */
            emit_lit(&hbuf, "/* Auto-generated by zserio-c parser. */\n");
            emit_lit(&hbuf, "#ifndef ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n");
            emit_lit(&hbuf, "#define ZSP_GENERATED_");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_H_\n\n");
            emit_lit(&hbuf, "#include <stdint.h>\n");
            emit_lit(&hbuf, "#include \"zserio/string.h\"\n");
            emit_lit(&hbuf, "#include \"zserio/bytes.h\"\n");
            emit_lit(&hbuf, "#include \"zserio/extern.h\"\n");
            /* Include the underlying type's header if it's a named ref.
             * Qualify with `../<pkg>/` when the target lives in a
             * different package than this subtype. */
            const zsp_ast_node_t *target = d->u.subtype.target_type;
            if ((target != NULL) && (target->kind == ZSP_AST_TYPE_REF)) {
                const zsp_ast_node_t *tname = target->u.type_ref.name;
                if ((tname != NULL) && (tname->kind == ZSP_AST_DOTTED_NAME)) {
                    tname = tname->u.dotted.parts[tname->u.dotted.n_parts - 1U];
                }
                const uint8_t *tn = NULL;
                size_t         tl = 0U;
                if (ident_span(source, tname, &tn, &tl)) {
                    char self_pkg[1024];
                    const size_t self_len = decl_package_path(
                            d, source, self_pkg, sizeof(self_pkg));
                    char dep_pkg[1024];
                    const size_t dep_len = decl_package_path(
                            target->resolved, source, dep_pkg,
                            sizeof(dep_pkg));
                    const bool same_pkg = ((self_len == dep_len)
                            && (memcmp(self_pkg, dep_pkg, dep_len) == 0));
                    emit_lit(&hbuf, "#include \"");
                    if (!same_pkg) {
                        size_t levels_up = 0U;
                        if (self_len > 0U) {
                            levels_up = 1U;
                            for (size_t k = 0U; k < self_len; ++k) {
                                if (self_pkg[k] == '/') levels_up += 1U;
                            }
                        }
                        for (size_t k = 0U; k < levels_up; ++k) {
                            emit_lit(&hbuf, "../");
                        }
                        if (dep_len > 0U) {
                            emit_bytes(&hbuf, dep_pkg, dep_len);
                            emit_lit(&hbuf, "/");
                        }
                    }
                    emit_snake(&hbuf, tn, tl);
                    emit_lit(&hbuf, ".h\"\n");
                }
            }
            emit_lit(&hbuf, "\n");
            emit_lit(&hbuf, "typedef ");
            bool owned = false;
            (void)emit_field_c_type(&hbuf, d->u.subtype.target_type, source, &owned);
            emit_lit(&hbuf, " ");
            (void)emit_type_id(&hbuf, d, source);
            emit_lit(&hbuf, "_t;\n\n#endif\n");
            if (hbuf.oom) return ZSERIO_ERR_OUT_OF_MEMORY;
            if (!write(userdata, path_h, hbuf.bytes, hbuf.len)) {
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
        }
    }

    return ZSERIO_OK;
}
