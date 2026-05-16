/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSPARSER_AST_H_
#define ZSPARSER_AST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zserio/status.h"

#include "zsparser/arena.h"
#include "zsparser/source.h"
#include "zsparser/token.h"

/**
 * @file
 * AST node definitions. Per Q2 (`docs/phase-7/scope.md`), a
 * single tagged-union node type covers all kinds; the arena
 * (zsparser/arena.h) owns every node, every children-list array,
 * and every interned identifier; nothing in the parser path uses
 * heap-per-node allocation.
 *
 * Every node carries `(file_id, offset, length)` covering the
 * source span it was parsed from. The locator shape is the same
 * as the token shape (`zsparser/token.h`); a node is a locator
 * plus a kind plus per-kind data.
 *
 * Child-list patterns: variable-length children (e.g. struct
 * members, enum items, call arguments) appear as
 * `(node ** items, size_t count)` pairs pointing into
 * arena-allocated arrays.
 */

typedef struct zsp_ast_node zsp_ast_node_t;

/* ----- builtin type codes (zsp_ast_type_builtin) ----- */
typedef enum zsp_builtin_code {
    ZSP_TB_UNKNOWN = 0,
    ZSP_TB_BOOL,
    ZSP_TB_INT8,    ZSP_TB_INT16,   ZSP_TB_INT32,   ZSP_TB_INT64,
    ZSP_TB_UINT8,   ZSP_TB_UINT16,  ZSP_TB_UINT32,  ZSP_TB_UINT64,
    ZSP_TB_VARINT,  ZSP_TB_VARINT16, ZSP_TB_VARINT32, ZSP_TB_VARINT64,
    ZSP_TB_VARUINT, ZSP_TB_VARUINT16, ZSP_TB_VARUINT32, ZSP_TB_VARUINT64,
    ZSP_TB_VARSIZE,
    ZSP_TB_FLOAT16, ZSP_TB_FLOAT32, ZSP_TB_FLOAT64,
    ZSP_TB_STRING,
    ZSP_TB_BYTES,
    ZSP_TB_EXTERN,
    /* bit:N and int:N use ZSP_TB_BIT/INT_FIELD with type_builtin.width */
    ZSP_TB_BIT_FIELD,
    ZSP_TB_INT_FIELD
} zsp_builtin_code_t;

/* ----- binary / unary operator codes ----- */
typedef enum zsp_op_code {
    ZSP_OP_UNKNOWN = 0,
    /* binary */
    ZSP_OP_ADD, ZSP_OP_SUB, ZSP_OP_MUL, ZSP_OP_DIV, ZSP_OP_MOD,
    ZSP_OP_LSHIFT, ZSP_OP_RSHIFT,
    ZSP_OP_BIT_AND, ZSP_OP_BIT_OR, ZSP_OP_BIT_XOR,
    ZSP_OP_LOGICAL_AND, ZSP_OP_LOGICAL_OR,
    ZSP_OP_EQ, ZSP_OP_NEQ, ZSP_OP_LT, ZSP_OP_GT, ZSP_OP_LE, ZSP_OP_GE,
    /* unary */
    ZSP_OP_NEG, ZSP_OP_BIT_NOT, ZSP_OP_LOGICAL_NOT,
    /* call-like */
    ZSP_OP_LENGTHOF, ZSP_OP_VALUEOF, ZSP_OP_NUMBITS, ZSP_OP_ISSET
} zsp_op_code_t;

/* ----- annotation codes ----- */
typedef enum zsp_annotation_code {
    ZSP_ANNOT_DEPRECATED,
    ZSP_ANNOT_REMOVED,
    ZSP_ANNOT_INDEX
} zsp_annotation_code_t;

/* ----- AST node kinds ----- */
typedef enum zsp_ast_kind {
    /* Top-level */
    ZSP_AST_UNIT,             /**< compilation unit */
    ZSP_AST_PACKAGE,
    ZSP_AST_IMPORT,
    ZSP_AST_COMPAT_VERSION,   /**< zserio_compatibility_version "x.y.z"; */

    /* Compound declarations */
    ZSP_AST_STRUCT,
    ZSP_AST_UNION,
    ZSP_AST_CHOICE,
    ZSP_AST_ENUM,
    ZSP_AST_BITMASK,
    ZSP_AST_SUBTYPE,
    ZSP_AST_CONST,
    ZSP_AST_INSTANTIATE,
    ZSP_AST_RULE_GROUP,
    ZSP_AST_RULE,
    ZSP_AST_SERVICE,
    ZSP_AST_PUBSUB,
    ZSP_AST_SQL_TABLE,
    ZSP_AST_SQL_DATABASE,

    /* Compound-body members */
    ZSP_AST_FIELD,
    ZSP_AST_ENUM_ITEM,
    ZSP_AST_BITMASK_ITEM,
    ZSP_AST_CHOICE_CASE,
    ZSP_AST_CHOICE_DEFAULT,
    ZSP_AST_FUNCTION,
    ZSP_AST_PARAMETER,
    ZSP_AST_TEMPLATE_PARAM,
    ZSP_AST_SERVICE_METHOD,
    ZSP_AST_PUBSUB_MESSAGE,
    ZSP_AST_SQL_FIELD,
    ZSP_AST_SQL_CONSTRAINT,
    ZSP_AST_SQL_TABLE_REF,

    /* Types */
    ZSP_AST_TYPE_BUILTIN,
    ZSP_AST_TYPE_REF,
    ZSP_AST_TYPE_ARRAY,

    /* Expressions */
    ZSP_AST_EXPR_INT_LIT,
    ZSP_AST_EXPR_FLOAT_LIT,
    ZSP_AST_EXPR_STRING_LIT,
    ZSP_AST_EXPR_BOOL_LIT,
    ZSP_AST_EXPR_IDENT,
    ZSP_AST_EXPR_BINARY,
    ZSP_AST_EXPR_UNARY,
    ZSP_AST_EXPR_TERNARY,
    ZSP_AST_EXPR_MEMBER,
    ZSP_AST_EXPR_INDEX,
    ZSP_AST_EXPR_CALL,

    /* Misc */
    ZSP_AST_ANNOTATION,
    ZSP_AST_DOTTED_NAME,

    ZSP_AST_KIND_COUNT
} zsp_ast_kind_t;

/* ----- node body unions ----- */

/* Atomic literals — payload is decoded value plus the source
 * span (already on the locator). */

struct zsp_ast_node {
    zsp_ast_kind_t kind;
    zsp_file_id_t  file_id;
    uint32_t       offset;
    uint32_t       length;

    /* Generic side-channel for analyser annotations. NULL on
     * freshly-parsed nodes; semantic analyser populates per-kind:
     *   - TYPE_REF       → pointer to the resolved declaration node
     *   - EXPR_IDENT     → pointer to the declaration the name binds to
     *   - SUBTYPE / etc. → may carry a resolved type node
     * Other kinds leave it NULL.
     */
    struct zsp_ast_node *resolved;
    /* Constant-evaluation result (set by the analyser when an
     * expression evaluates to a compile-time constant). NULL
     * on freshly-parsed nodes and on expressions that are
     * non-constant or have not yet been evaluated. Points to a
     * `zsp_sema_const_t` allocated in the same arena.
     */
    void *computed;

    union {
        /* literals */
        struct { uint64_t value; }            int_lit;
        struct { double   value; }            float_lit;
        struct { uint32_t span_off;
                 uint32_t span_len; }         string_lit;   /**< source bytes incl. quotes */
        struct { bool     value; }            bool_lit;

        /* identifier reference — source span carries the spelling */
        struct { uint32_t span_off;
                 uint32_t span_len; }         ident;

        /* dotted name: list of ident nodes */
        struct { zsp_ast_node_t **parts;
                 size_t            n_parts; } dotted;

        /* expressions */
        struct { zsp_op_code_t   op;
                 zsp_ast_node_t *lhs;
                 zsp_ast_node_t *rhs; }       binop;
        struct { zsp_op_code_t   op;
                 zsp_ast_node_t *operand; }   unop;
        struct { zsp_ast_node_t *cond;
                 zsp_ast_node_t *then_expr;
                 zsp_ast_node_t *else_expr; } ternary;
        struct { zsp_ast_node_t *lhs;
                 zsp_ast_node_t *name; }      member;
        struct { zsp_ast_node_t *lhs;
                 zsp_ast_node_t *index; }     index;
        struct { zsp_ast_node_t  *callee;
                 zsp_ast_node_t **args;
                 size_t            n_args; }  call;

        /* types */
        struct { zsp_builtin_code_t code;
                 uint32_t           width; }  type_builtin;
        struct { zsp_ast_node_t  *name;            /**< DOTTED_NAME or IDENT */
                 zsp_ast_node_t **template_args;   /**< Types */
                 size_t            n_template_args;
                 zsp_ast_node_t **type_args;       /**< Expressions (params) */
                 size_t            n_type_args; }  type_ref;
        struct { zsp_ast_node_t *element;
                 zsp_ast_node_t *length_expr;      /**< NULL = auto */
                 bool             packed;
                 bool             implicit; }      type_array;

        /* compound declarations (struct/union/sql_table/sql_database) */
        struct { zsp_ast_node_t **template_params;
                 size_t            n_template_params;
                 zsp_ast_node_t  *name;
                 zsp_ast_node_t **params;
                 size_t            n_params;
                 zsp_ast_node_t **members;
                 size_t            n_members; }    compound;

        /* choice (struct + selector_expr) */
        struct { zsp_ast_node_t **template_params;
                 size_t            n_template_params;
                 zsp_ast_node_t  *name;
                 zsp_ast_node_t **params;
                 size_t            n_params;
                 zsp_ast_node_t  *selector;        /**< Expression */
                 zsp_ast_node_t **cases;
                 size_t            n_cases;
                 zsp_ast_node_t  *default_case; }  choice;

        struct { zsp_ast_node_t **labels;          /**< Expression list */
                 size_t            n_labels;
                 zsp_ast_node_t  *field; }         choice_case;

        struct { zsp_ast_node_t *field; }          choice_default;

        struct { zsp_ast_node_t *name;
                 zsp_ast_node_t *base_type;
                 zsp_ast_node_t **items;
                 size_t            n_items; }      enum_decl;

        struct { zsp_ast_node_t *name;
                 zsp_ast_node_t *value_expr; }     enum_item;

        struct { zsp_ast_node_t *name;
                 zsp_ast_node_t *base_type;
                 zsp_ast_node_t **items;
                 size_t            n_items; }      bitmask_decl;

        struct { zsp_ast_node_t *name;
                 zsp_ast_node_t *value_expr; }     bitmask_item;

        struct { zsp_ast_node_t **template_params;
                 size_t            n_template_params;
                 zsp_ast_node_t  *target_type;
                 zsp_ast_node_t  *alias_name; }    subtype;

        struct { zsp_ast_node_t *type;
                 zsp_ast_node_t *name;
                 zsp_ast_node_t *value_expr; }     const_decl;

        struct { zsp_ast_node_t *return_type;
                 zsp_ast_node_t *name;
                 zsp_ast_node_t *body_expr; }      function_decl;

        struct { zsp_ast_node_t *name;
                 zsp_ast_node_t *target; }         instantiate;

        struct { zsp_ast_node_t *name;
                 zsp_ast_node_t **rules;
                 size_t            n_rules; }      rule_group;

        struct { zsp_ast_node_t *name;
                 zsp_ast_node_t *id_expr;
                 zsp_ast_node_t *body_expr; }      rule;

        struct { zsp_ast_node_t  *name;
                 zsp_ast_node_t **methods;
                 size_t            n_methods; }    service;

        struct { zsp_ast_node_t *return_type;
                 zsp_ast_node_t *name;
                 zsp_ast_node_t *request_type; }   service_method;

        struct { zsp_ast_node_t  *name;
                 zsp_ast_node_t **messages;
                 size_t            n_messages; }   pubsub;

        struct { zsp_ast_node_t *topic_expr;
                 zsp_ast_node_t *type;
                 zsp_ast_node_t *name;
                 bool             publish;
                 bool             subscribe; }     pubsub_message;

        /* fields (struct/union/sql) */
        struct { zsp_ast_node_t **annotations;
                 size_t            n_annotations;
                 zsp_ast_node_t  *align_expr;
                 zsp_ast_node_t  *offset_expr;
                 zsp_ast_node_t  *type;
                 zsp_ast_node_t  *name;
                 zsp_ast_node_t  *init_expr;
                 zsp_ast_node_t  *if_clause;
                 zsp_ast_node_t  *constraint_expr;
                 bool             optional;
                 bool             implicit;
                 bool             autoid; }        field;

        struct { zsp_ast_node_t *type;
                 zsp_ast_node_t *name; }           parameter;

        struct { zsp_ast_node_t *name; }           template_param;

        /* SQL table specifics */
        struct { zsp_ast_node_t  *name;
                 zsp_ast_node_t **fields;
                 size_t            n_fields;
                 zsp_ast_node_t **constraints;
                 size_t            n_constraints;
                 bool              virtual_;
                 bool              without_rowid;
                 zsp_ast_node_t   *virtual_using; } sql_table;

        struct { zsp_ast_node_t *type;
                 zsp_ast_node_t *name;
                 zsp_ast_node_t *constraint_expr;
                 bool             optional; }      sql_field;

        struct { zsp_ast_node_t *expr; }           sql_constraint;

        struct { zsp_ast_node_t  *name;
                 zsp_ast_node_t **table_refs;
                 size_t            n_table_refs; } sql_database;

        struct { zsp_ast_node_t *type;
                 zsp_ast_node_t *name; }           sql_table_ref;

        /* annotation (@deprecated, @removed [(msg)], @index) */
        struct { zsp_annotation_code_t code;
                 zsp_ast_node_t       *message; }  annotation;

        /* top-level structure */
        struct { zsp_ast_node_t  *package;
                 zsp_ast_node_t  *compat_version;
                 zsp_ast_node_t **imports;
                 size_t            n_imports;
                 zsp_ast_node_t **decls;
                 size_t            n_decls; }      unit;

        struct { zsp_ast_node_t *name; }           package;

        struct { zsp_ast_node_t *name;          /**< Full dotted path. */
                 bool             wildcard;     /**< True for `import path.*;`. */ }
                                                  import;

        struct { zsp_ast_node_t *version_string; } compat_version;
    } u;
};

/* ----- Helpers ----- */

/**
 * Allocate a new zero-initialised node in the arena, with the
 * supplied kind + locator pre-filled. Returns NULL on OOM.
 */
zsp_ast_node_t *zsp_ast_new(zsp_arena_t *a, zsp_ast_kind_t kind,
                            zsp_file_id_t file_id,
                            uint32_t offset, uint32_t length);

/** Returns a static printable name for `kind` for diagnostics. */
const char *zsp_ast_kind_name(zsp_ast_kind_t kind);

/* ----- Append-builder for arena-allocated child arrays ----- */

typedef struct zsp_ast_list {
    zsp_arena_t      *arena;
    zsp_ast_node_t  **items;
    size_t            count;
    size_t            cap;
} zsp_ast_list_t;

void zsp_ast_list_init(zsp_ast_list_t *l, zsp_arena_t *a);

/** Push a node onto the list, growing the backing array as needed.
 *  Returns ZSERIO_ERR_OUT_OF_MEMORY if the arena cannot satisfy
 *  the growth. */
zserio_status_t zsp_ast_list_push(zsp_ast_list_t *l, zsp_ast_node_t *n);

/** Finalise the list into a stable `(items, count)` pair to store
 *  on a parent node. The list builder is consumed and the caller
 *  must not use it further (no-op safe; underlying arena keeps
 *  ownership of the items). */
void zsp_ast_list_take(zsp_ast_list_t *l,
                       zsp_ast_node_t ***out_items, size_t *out_count);

#endif /* ZSPARSER_AST_H_ */
