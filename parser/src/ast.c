/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zsparser/ast.h"

#include <string.h>

zsp_ast_node_t *zsp_ast_new(zsp_arena_t *a, zsp_ast_kind_t kind,
                            zsp_file_id_t file_id,
                            uint32_t offset, uint32_t length)
{
    zsp_ast_node_t *const n = (zsp_ast_node_t *)zsp_arena_alloc(
            a, sizeof(zsp_ast_node_t), sizeof(void *));
    if (n == NULL) {
        return NULL;
    }
    n->kind    = kind;
    n->file_id = file_id;
    n->offset  = offset;
    n->length  = length;
    return n;
}

const char *zsp_ast_kind_name(zsp_ast_kind_t kind)
{
    switch (kind) {
    case ZSP_AST_UNIT:              return "unit";
    case ZSP_AST_PACKAGE:           return "package";
    case ZSP_AST_IMPORT:            return "import";
    case ZSP_AST_COMPAT_VERSION:    return "compat-version";
    case ZSP_AST_STRUCT:            return "struct";
    case ZSP_AST_UNION:             return "union";
    case ZSP_AST_CHOICE:            return "choice";
    case ZSP_AST_ENUM:              return "enum";
    case ZSP_AST_BITMASK:           return "bitmask";
    case ZSP_AST_SUBTYPE:           return "subtype";
    case ZSP_AST_CONST:             return "const";
    case ZSP_AST_INSTANTIATE:       return "instantiate";
    case ZSP_AST_RULE_GROUP:        return "rule-group";
    case ZSP_AST_RULE:              return "rule";
    case ZSP_AST_SERVICE:           return "service";
    case ZSP_AST_PUBSUB:            return "pubsub";
    case ZSP_AST_SQL_TABLE:         return "sql-table";
    case ZSP_AST_SQL_DATABASE:      return "sql-database";
    case ZSP_AST_FIELD:             return "field";
    case ZSP_AST_ENUM_ITEM:         return "enum-item";
    case ZSP_AST_BITMASK_ITEM:      return "bitmask-item";
    case ZSP_AST_CHOICE_CASE:       return "choice-case";
    case ZSP_AST_CHOICE_DEFAULT:    return "choice-default";
    case ZSP_AST_FUNCTION:          return "function";
    case ZSP_AST_PARAMETER:         return "parameter";
    case ZSP_AST_TEMPLATE_PARAM:    return "template-param";
    case ZSP_AST_SERVICE_METHOD:    return "service-method";
    case ZSP_AST_PUBSUB_MESSAGE:    return "pubsub-message";
    case ZSP_AST_SQL_FIELD:         return "sql-field";
    case ZSP_AST_SQL_CONSTRAINT:    return "sql-constraint";
    case ZSP_AST_SQL_TABLE_REF:     return "sql-table-ref";
    case ZSP_AST_TYPE_BUILTIN:      return "type-builtin";
    case ZSP_AST_TYPE_REF:          return "type-ref";
    case ZSP_AST_TYPE_ARRAY:        return "type-array";
    case ZSP_AST_EXPR_INT_LIT:      return "int-lit";
    case ZSP_AST_EXPR_FLOAT_LIT:    return "float-lit";
    case ZSP_AST_EXPR_STRING_LIT:   return "string-lit";
    case ZSP_AST_EXPR_BOOL_LIT:     return "bool-lit";
    case ZSP_AST_EXPR_IDENT:        return "ident";
    case ZSP_AST_EXPR_BINARY:       return "binop";
    case ZSP_AST_EXPR_UNARY:        return "unop";
    case ZSP_AST_EXPR_TERNARY:      return "ternary";
    case ZSP_AST_EXPR_MEMBER:       return "member";
    case ZSP_AST_EXPR_INDEX:        return "index";
    case ZSP_AST_EXPR_CALL:         return "call";
    case ZSP_AST_ANNOTATION:        return "annotation";
    case ZSP_AST_DOTTED_NAME:       return "dotted-name";
    case ZSP_AST_KIND_COUNT:        return "<invalid-ast-kind>";
    }
    return "<invalid-ast-kind>";
}

/* ----- list builder ------------------------------------------ */

#define ZSP_LIST_INITIAL_CAP ((size_t)4U)

void zsp_ast_list_init(zsp_ast_list_t *l, zsp_arena_t *a)
{
    l->arena = a;
    l->items = NULL;
    l->count = 0U;
    l->cap   = 0U;
}

zserio_status_t zsp_ast_list_push(zsp_ast_list_t *l, zsp_ast_node_t *n)
{
    if (l == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (l->count == l->cap) {
        const size_t new_cap = (l->cap == 0U)
                ? ZSP_LIST_INITIAL_CAP : (l->cap * 2U);
        zsp_ast_node_t **const grown = (zsp_ast_node_t **)zsp_arena_alloc(
                l->arena, new_cap * sizeof(zsp_ast_node_t *),
                sizeof(zsp_ast_node_t *));
        if (grown == NULL) {
            return ZSERIO_ERR_OUT_OF_MEMORY;
        }
        if (l->items != NULL) {
            (void)memcpy(grown, l->items, l->count * sizeof(zsp_ast_node_t *));
        }
        l->items = grown;
        l->cap   = new_cap;
    }
    l->items[l->count] = n;
    l->count += 1U;
    return ZSERIO_OK;
}

void zsp_ast_list_take(zsp_ast_list_t *l,
                       zsp_ast_node_t ***out_items, size_t *out_count)
{
    *out_items = l->items;
    *out_count = l->count;
    /* Leave the builder in a defined empty-ish state so any
     * further `push` would reallocate fresh. The take operation
     * does NOT shrink the cap to count — that would require
     * an extra copy. The arena owns the trailing slack until
     * the arena itself is disposed; this is the normal
     * arena-allocation tradeoff. */
    l->items = NULL;
    l->count = 0U;
    l->cap   = 0U;
}
