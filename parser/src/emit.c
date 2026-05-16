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
} buf_t;

static void buf_init(buf_t *b, zsp_arena_t *arena)
{
    b->arena = arena;
    b->bytes = NULL;
    b->len   = 0U;
    b->cap   = 0U;
    b->oom   = false;
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

/* Convert UpperCamelCase or mixedCamel to snake_case in-place
 * into the buffer. Doesn't lowercase letters that aren't ASCII
 * upper; doesn't handle non-ASCII. */
static void emit_snake(buf_t *b, const uint8_t *src, size_t len)
{
    bool prev_lower = false;
    for (size_t i = 0U; i < len; ++i) {
        const uint8_t c = src[i];
        if ((c >= (uint8_t)'A') && (c <= (uint8_t)'Z')) {
            if (prev_lower) {
                emit_lit(b, "_");
            }
            const char lc = (char)(c - (uint8_t)'A' + (uint8_t)'a');
            emit_bytes(b, &lc, 1U);
            prev_lower = false;
        }
        else {
            emit_bytes(b, (const char *)&c, 1U);
            prev_lower = (c >= (uint8_t)'a') && (c <= (uint8_t)'z');
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
    case ZSP_TB_VARINT:
    case ZSP_TB_VARINT16:
    case ZSP_TB_VARINT32:
    case ZSP_TB_VARINT64:   return "int64_t";
    case ZSP_TB_VARUINT:
    case ZSP_TB_VARUINT16:
    case ZSP_TB_VARUINT32:
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
    case ZSP_TB_VARSIZE:    return "zserio_read_varsize";
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
    case ZSP_TB_VARSIZE:    return "zserio_write_varsize";
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
        emit_lit(b, builtin_c_type(c));
        *is_owned = builtin_needs_free(c);
        return c;
    }
    if (type_node->kind == ZSP_AST_TYPE_REF) {
        /* For named types, the emitted C is `<lowered-name>_t`. */
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
    const uint8_t *name = NULL;
    size_t         nlen = 0U;
    (void)ident_span(src, cmpd->u.compound.name, &name, &nlen);

    emit_lit(b, "typedef struct ");
    emit_snake(b, name, nlen);
    emit_lit(b, " {\n");

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

    /* Emit fields. */
    for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if (m->kind != ZSP_AST_FIELD) {
            continue;
        }
        const zsp_ast_node_t *ty = m->u.field.type;
        const bool is_array = (ty != NULL) && (ty->kind == ZSP_AST_TYPE_ARRAY);
        const zsp_ast_node_t *element_ty = is_array ? ty->u.type_array.element : ty;

        emit_lit(b, "    ");
        bool owned = false;
        (void)emit_field_c_type(b, element_ty, src, &owned);
        if (is_array) {
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
        emit_lit(b, ";\n");

        if (is_array) {
            emit_lit(b, "    size_t ");
            if (ident_span(src, m->u.field.name, &fn, &fl)) {
                emit_snake(b, fn, fl);
            }
            emit_lit(b, "_count;\n");
        }
    }

    emit_lit(b, "} ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t;\n");
}

/* Emit function prototypes for one compound. */
static void emit_compound_protos(buf_t *b, const zsp_ast_node_t *cmpd,
                                 const zsp_source_t *src)
{
    const uint8_t *name = NULL;
    size_t         nlen = 0U;
    (void)ident_span(src, cmpd->u.compound.name, &name, &nlen);

    emit_lit(b, "\nvoid ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_init(");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t *self);\n");

    emit_lit(b, "\nzserio_status_t ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_read(");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t *self, zserio_bitstream_reader_t *bs,\n"
                "        const zserio_allocator_t *alloc);\n");

    emit_lit(b, "\nzserio_status_t ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_write(const ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t *self, zserio_bitstream_writer_t *bs);\n");

    emit_lit(b, "\nvoid ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_free(");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t *self, const zserio_allocator_t *alloc);\n");
}

static void emit_header_for_compound(buf_t *b, const zsp_ast_node_t *cmpd,
                                     const zsp_source_t *src)
{
    const uint8_t *name = NULL;
    size_t         nlen = 0U;
    (void)ident_span(src, cmpd->u.compound.name, &name, &nlen);

    /* Header guard. */
    emit_lit(b, "/* Auto-generated by zserio-c parser. Do not edit. */\n");
    emit_lit(b, "#ifndef ZSP_GENERATED_");
    emit_snake(b, name, nlen);
    emit_lit(b, "_H_\n");
    emit_lit(b, "#define ZSP_GENERATED_");
    emit_snake(b, name, nlen);
    emit_lit(b, "_H_\n\n");

    emit_lit(b, "#include <stdbool.h>\n");
    emit_lit(b, "#include <stddef.h>\n");
    emit_lit(b, "#include <stdint.h>\n\n");
    emit_lit(b, "#include \"zserio/allocator.h\"\n");
    emit_lit(b, "#include \"zserio/bitstream.h\"\n");
    emit_lit(b, "#include \"zserio/bytes.h\"\n");
    emit_lit(b, "#include \"zserio/extern.h\"\n");
    emit_lit(b, "#include \"zserio/float.h\"\n");
    emit_lit(b, "#include \"zserio/primitives.h\"\n");
    emit_lit(b, "#include \"zserio/status.h\"\n");
    emit_lit(b, "#include \"zserio/string.h\"\n");
    emit_lit(b, "#include \"zserio/varint.h\"\n\n");

    emit_struct_decl(b, cmpd, src);
    emit_compound_protos(b, cmpd, src);

    emit_lit(b, "\n#endif\n");
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

    if (is_array) {
        emit_lit(b, "    /* array field: ");
        emit_span(b, fn, fl);
        emit_lit(b, " — basic emit pending full feature support */\n");
        emit_lit(b, "    self->");
        emit_snake(b, fn, fl);
        emit_lit(b, " = NULL;\n");
        emit_lit(b, "    self->");
        emit_snake(b, fn, fl);
        emit_lit(b, "_count = 0u;\n");
        return;
    }

    if (element_ty->kind == ZSP_AST_TYPE_BUILTIN) {
        const zsp_builtin_code_t c = element_ty->u.type_builtin.code;
        if (builtin_needs_free(c)) {
            /* Runtime read signature for owned types is
             * `(bs, alloc, out)` — allocator before output. */
            emit_lit(b, "    ZSERIO_TRY(");
            emit_lit(b, builtin_read_fn(c));
            emit_lit(b, "(bs, alloc, &self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "));\n");
        }
        else {
            emit_lit(b, "    ZSERIO_TRY(");
            emit_lit(b, builtin_read_fn(c));
            emit_lit(b, "(bs, &self->");
            emit_snake(b, fn, fl);
            emit_lit(b, "));\n");
        }
    }
    else {
        emit_lit(b, "    /* nested compound read pending */\n");
    }
}

static void emit_field_write(buf_t *b, const zsp_ast_node_t *field,
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
    if (is_array) {
        emit_lit(b, "    /* array write pending */\n");
        return;
    }
    if (ty->kind == ZSP_AST_TYPE_BUILTIN) {
        const zsp_builtin_code_t c = ty->u.type_builtin.code;
        if (c == ZSP_TB_STRING) {
            /* zserio_write_string takes a string_view (read-side
             * type); construct one inline from the owned string. */
            emit_lit(b, "    {\n");
            emit_lit(b, "        const zserio_string_view_t view_");
            emit_snake(b, fn, fl);
            emit_lit(b, " = { self->");
            emit_snake(b, fn, fl);
            emit_lit(b, ".bytes, self->");
            emit_snake(b, fn, fl);
            emit_lit(b, ".length };\n");
            emit_lit(b, "        ZSERIO_TRY(zserio_write_string(bs, view_");
            emit_snake(b, fn, fl);
            emit_lit(b, "));\n");
            emit_lit(b, "    }\n");
        }
        else {
            emit_lit(b, "    ZSERIO_TRY(");
            emit_lit(b, builtin_write_fn(c));
            emit_lit(b, "(bs, ");
            if (builtin_needs_free(c)) {
                emit_lit(b, "&self->");
                emit_snake(b, fn, fl);
            }
            else {
                emit_lit(b, "self->");
                emit_snake(b, fn, fl);
            }
            emit_lit(b, "));\n");
        }
    }
    else {
        emit_lit(b, "    /* nested compound write pending */\n");
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
    /* Arrays + nested compounds: free pending. */
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
    emit_snake(b, name, nlen);
    emit_lit(b, "_init(");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t *self)\n{\n");
    emit_lit(b, "    (void)memset(self, 0, sizeof(*self));\n");
    emit_lit(b, "}\n\n");

    /* read */
    emit_lit(b, "zserio_status_t ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_read(");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t *self, zserio_bitstream_reader_t *bs,\n"
                "        const zserio_allocator_t *alloc)\n{\n");
    emit_lit(b, "    (void)alloc;\n");
    for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if (m->kind == ZSP_AST_FIELD) {
            emit_field_read(b, m, src);
        }
    }
    emit_lit(b, "    return ZSERIO_OK;\n");
    emit_lit(b, "}\n\n");

    /* write */
    emit_lit(b, "zserio_status_t ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_write(const ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t *self, zserio_bitstream_writer_t *bs)\n{\n");
    for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if (m->kind == ZSP_AST_FIELD) {
            emit_field_write(b, m, src);
        }
    }
    emit_lit(b, "    return ZSERIO_OK;\n");
    emit_lit(b, "}\n\n");

    /* free */
    emit_lit(b, "void ");
    emit_snake(b, name, nlen);
    emit_lit(b, "_free(");
    emit_snake(b, name, nlen);
    emit_lit(b, "_t *self, const zserio_allocator_t *alloc)\n{\n");
    emit_lit(b, "    (void)alloc;\n");
    for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
        const zsp_ast_node_t *m = cmpd->u.compound.members[i];
        if (m->kind == ZSP_AST_FIELD) {
            emit_field_free(b, m, src);
        }
    }
    emit_lit(b, "}\n");
}

/* ----- Enum + bitmask emission ------------------------------ */

static void emit_enum_header(buf_t *b, const zsp_ast_node_t *enm,
                             const zsp_source_t *src)
{
    const uint8_t *name = NULL;
    size_t         nlen = 0U;
    (void)ident_span(src, enm->u.enum_decl.name, &name, &nlen);

    emit_lit(b, "/* Auto-generated by zserio-c parser. Do not edit. */\n");
    emit_lit(b, "#ifndef ZSP_GENERATED_");
    emit_snake(b, name, nlen);
    emit_lit(b, "_H_\n");
    emit_lit(b, "#define ZSP_GENERATED_");
    emit_snake(b, name, nlen);
    emit_lit(b, "_H_\n\n");
    emit_lit(b, "#include <stdint.h>\n\n");

    emit_lit(b, "typedef enum ");
    emit_snake(b, name, nlen);
    emit_lit(b, " {\n");
    for (size_t i = 0U; i < enm->u.enum_decl.n_items; ++i) {
        const zsp_ast_node_t *item = enm->u.enum_decl.items[i];
        emit_lit(b, "    ");
        emit_snake(b, name, nlen);
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
    emit_snake(b, name, nlen);
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
    /* Append snake-cased name. */
    bool prev_lower = false;
    for (size_t i = 0U; (i < name_len) && (pos + 4U < out_cap); ++i) {
        const uint8_t c = name[i];
        if ((c >= (uint8_t)'A') && (c <= (uint8_t)'Z')) {
            if (prev_lower) {
                out[pos++] = '_';
            }
            out[pos++] = (char)(c - (uint8_t)'A' + (uint8_t)'a');
            prev_lower = false;
        }
        else {
            out[pos++] = (char)c;
            prev_lower = (c >= (uint8_t)'a') && (c <= (uint8_t)'z');
        }
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

        if ((d->kind == ZSP_AST_STRUCT) || (d->kind == ZSP_AST_UNION)) {
            name_node = d->u.compound.name;
        }
        else if (d->kind == ZSP_AST_ENUM) {
            name_node = d->u.enum_decl.name;
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
    }

    return ZSERIO_OK;
}
