/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zsparser/sema.h"

#include <string.h>

/*
 * Semantic analyser — first slice. Builds a symbol table over
 * top-level declarations, walks the AST resolving type-ref and
 * identifier references, emits diagnostics for duplicates and
 * unresolved names. Resolved declarations land on
 * `node->resolved`.
 *
 * Scope stack is allocated from the same arena as the AST.
 * Visitor is iterative-with-stack (no recursion past the
 * recursive-descent deviation set in Q5; the scope-tree walk
 * is bounded by AST nesting depth).
 */

static const uint8_t *source_bytes(const zsp_sema_t *s, zsp_file_id_t fid)
{
    const zsp_file_t *f = NULL;
    if (!zsp_source_get(s->source, fid, &f)) {
        return NULL;
    }
    return f->bytes;
}

/* ----- Scope construction ------------------------------------- */

static zserio_status_t scope_new(zsp_sema_t *s, zsp_sema_scope_t *parent,
                                 zsp_sema_scope_t **out)
{
    zsp_sema_scope_t *const sc = (zsp_sema_scope_t *)zsp_arena_alloc(
            s->arena, sizeof(zsp_sema_scope_t), sizeof(void *));
    if (sc == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    sc->parent   = parent;
    sc->bindings = NULL;
    sc->count    = 0U;
    sc->cap      = 0U;
    *out = sc;
    return ZSERIO_OK;
}

static zserio_status_t scope_grow(zsp_sema_t *s, zsp_sema_scope_t *sc)
{
    if (sc->count < sc->cap) {
        return ZSERIO_OK;
    }
    const size_t new_cap = (sc->cap == 0U) ? 8U : (sc->cap * 2U);
    zsp_sema_binding_t *const grown = (zsp_sema_binding_t *)zsp_arena_alloc(
            s->arena, new_cap * sizeof(zsp_sema_binding_t),
            sizeof(zsp_sema_binding_t));
    if (grown == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    if (sc->bindings != NULL) {
        (void)memcpy(grown, sc->bindings,
                     sc->count * sizeof(zsp_sema_binding_t));
    }
    sc->bindings = grown;
    sc->cap      = new_cap;
    return ZSERIO_OK;
}

/* ----- Name fetch from AST nodes ------------------------------ */

/* Extract `(bytes, len)` for an IDENT or DOTTED_NAME's last
 * segment. Returns false if `n` doesn't have a name we can
 * extract. */
static bool extract_simple_name(const zsp_sema_t *s,
                                const zsp_ast_node_t *n,
                                const uint8_t **out_bytes, size_t *out_len)
{
    if (n == NULL) {
        return false;
    }
    const zsp_ast_node_t *target = n;
    if (n->kind == ZSP_AST_DOTTED_NAME) {
        if (n->u.dotted.n_parts == 0U) {
            return false;
        }
        target = n->u.dotted.parts[n->u.dotted.n_parts - 1U];
    }
    if (target->kind != ZSP_AST_EXPR_IDENT) {
        return false;
    }
    const uint8_t *const src = source_bytes(s, target->file_id);
    if (src == NULL) {
        return false;
    }
    *out_bytes = &src[target->u.ident.span_off];
    *out_len   = (size_t)target->u.ident.span_len;
    return true;
}

zsp_ast_node_t *zsp_sema_scope_find(const zsp_sema_scope_t *scope,
                                    const uint8_t *name, size_t len)
{
    const zsp_sema_scope_t *sc = scope;
    while (sc != NULL) {
        for (size_t i = 0U; i < sc->count; ++i) {
            const zsp_sema_binding_t *const b = &sc->bindings[i];
            if ((b->name_len == len)
                    && (memcmp(b->name, name, len) == 0)) {
                return b->decl;
            }
        }
        sc = sc->parent;
    }
    return NULL;
}

static zserio_status_t scope_bind(zsp_sema_t *s, zsp_sema_scope_t *sc,
                                  const uint8_t *name, size_t len,
                                  zsp_ast_node_t *decl)
{
    for (size_t i = 0U; i < sc->count; ++i) {
        const zsp_sema_binding_t *const b = &sc->bindings[i];
        if ((b->name_len == len) && (memcmp(b->name, name, len) == 0)) {
            (void)zsp_diag_emit_lit(s->diag, ZSP_SEV_ERROR,
                                    decl->file_id, decl->offset,
                                    "duplicate top-level declaration");
            return ZSERIO_OK;   /* keep parsing; not a fatal error */
        }
    }
    ZSERIO_TRY(scope_grow(s, sc));
    sc->bindings[sc->count].name     = name;
    sc->bindings[sc->count].name_len = len;
    sc->bindings[sc->count].decl     = decl;
    sc->count += 1U;
    return ZSERIO_OK;
}

/* ----- Pass 1: collect top-level declarations ----------------- */

/* Returns the IDENT node carrying the name a declaration binds. */
static const zsp_ast_node_t *decl_name_node(const zsp_ast_node_t *d)
{
    switch (d->kind) {
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:        return d->u.compound.name;
    case ZSP_AST_CHOICE:       return d->u.choice.name;
    case ZSP_AST_ENUM:         return d->u.enum_decl.name;
    case ZSP_AST_BITMASK:      return d->u.bitmask_decl.name;
    case ZSP_AST_SUBTYPE:      return d->u.subtype.alias_name;
    case ZSP_AST_CONST:        return d->u.const_decl.name;
    case ZSP_AST_INSTANTIATE:  return d->u.instantiate.name;
    case ZSP_AST_RULE_GROUP:   return d->u.rule_group.name;
    case ZSP_AST_SERVICE:      return d->u.service.name;
    case ZSP_AST_PUBSUB:       return d->u.pubsub.name;
    case ZSP_AST_SQL_TABLE:    return d->u.sql_table.name;
    case ZSP_AST_SQL_DATABASE: return d->u.sql_database.name;
    default:                   return NULL;
    }
}

static zserio_status_t collect_top_level(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    if (unit->kind != ZSP_AST_UNIT) {
        return ZSERIO_OK;
    }
    for (size_t i = 0U; i < unit->u.unit.n_decls; ++i) {
        zsp_ast_node_t *const d = unit->u.unit.decls[i];
        if (d == NULL) {
            continue;
        }
        const zsp_ast_node_t *const name = decl_name_node(d);
        const uint8_t *bytes = NULL;
        size_t         len   = 0U;
        if (!extract_simple_name(s, name, &bytes, &len)) {
            continue;
        }
        ZSERIO_TRY(scope_bind(s, s->root, bytes, len, d));
    }
    return ZSERIO_OK;
}

/* ----- Pass 2: resolve type-refs and identifiers -------------- */

/* Resolve TYPE_REF by looking up its name in the current scope.
 * Falls back to the last segment of the dotted name; package-qualified
 * lookup is a follow-up slice. */
static void resolve_type_ref(zsp_sema_t *s, zsp_ast_node_t *t)
{
    if (t->resolved != NULL) {
        return;
    }
    const uint8_t *bytes = NULL;
    size_t         len   = 0U;
    if (!extract_simple_name(s, t->u.type_ref.name, &bytes, &len)) {
        return;
    }
    zsp_ast_node_t *const decl = zsp_sema_scope_find(s->current, bytes, len);
    if (decl == NULL) {
        (void)zsp_diag_emit_lit(s->diag, ZSP_SEV_ERROR,
                                t->file_id, t->offset,
                                "reference to undeclared type");
        return;
    }
    t->resolved = decl;
}

/* Iterative AST walker. The work list holds nodes to visit; for
 * each we resolve and enqueue children. Order doesn't matter for
 * pure reference resolution. */
typedef struct walk_stack {
    zsp_ast_node_t **items;
    size_t            count;
    size_t            cap;
    zsp_arena_t      *arena;
    bool              oom;
} walk_stack_t;

static void walk_push(walk_stack_t *st, zsp_ast_node_t *n)
{
    if (n == NULL) {
        return;
    }
    if (st->count == st->cap) {
        const size_t new_cap = (st->cap == 0U) ? 32U : (st->cap * 2U);
        zsp_ast_node_t **const grown = (zsp_ast_node_t **)zsp_arena_alloc(
                st->arena, new_cap * sizeof(zsp_ast_node_t *),
                sizeof(zsp_ast_node_t *));
        if (grown == NULL) {
            st->oom = true;
            return;
        }
        if (st->items != NULL) {
            (void)memcpy(grown, st->items, st->count * sizeof(zsp_ast_node_t *));
        }
        st->items = grown;
        st->cap   = new_cap;
    }
    st->items[st->count] = n;
    st->count += 1U;
}

static void walk_push_list(walk_stack_t *st,
                           zsp_ast_node_t **items, size_t n)
{
    for (size_t i = 0U; i < n; ++i) {
        walk_push(st, items[i]);
    }
}

static void enqueue_children(walk_stack_t *st, zsp_ast_node_t *n)
{
    if (n == NULL) {
        return;
    }
    switch (n->kind) {
    case ZSP_AST_UNIT:
        walk_push(st, n->u.unit.package);
        walk_push(st, n->u.unit.compat_version);
        walk_push_list(st, n->u.unit.imports, n->u.unit.n_imports);
        walk_push_list(st, n->u.unit.decls,   n->u.unit.n_decls);
        break;
    case ZSP_AST_PACKAGE:
        walk_push(st, n->u.package.name);
        break;
    case ZSP_AST_IMPORT:
        walk_push(st, n->u.import.name);
        break;
    case ZSP_AST_DOTTED_NAME:
        walk_push_list(st, n->u.dotted.parts, n->u.dotted.n_parts);
        break;
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:
        walk_push(st, n->u.compound.name);
        walk_push_list(st, n->u.compound.template_params,
                       n->u.compound.n_template_params);
        walk_push_list(st, n->u.compound.params, n->u.compound.n_params);
        walk_push_list(st, n->u.compound.members, n->u.compound.n_members);
        break;
    case ZSP_AST_CHOICE:
        walk_push(st, n->u.choice.name);
        walk_push_list(st, n->u.choice.template_params,
                       n->u.choice.n_template_params);
        walk_push_list(st, n->u.choice.params, n->u.choice.n_params);
        walk_push(st, n->u.choice.selector);
        walk_push_list(st, n->u.choice.cases, n->u.choice.n_cases);
        walk_push(st, n->u.choice.default_case);
        break;
    case ZSP_AST_CHOICE_CASE:
        walk_push_list(st, n->u.choice_case.labels, n->u.choice_case.n_labels);
        walk_push(st, n->u.choice_case.field);
        break;
    case ZSP_AST_CHOICE_DEFAULT:
        walk_push(st, n->u.choice_default.field);
        break;
    case ZSP_AST_ENUM:
        walk_push(st, n->u.enum_decl.name);
        walk_push(st, n->u.enum_decl.base_type);
        walk_push_list(st, n->u.enum_decl.items, n->u.enum_decl.n_items);
        break;
    case ZSP_AST_ENUM_ITEM:
        walk_push(st, n->u.enum_item.name);
        walk_push(st, n->u.enum_item.value_expr);
        break;
    case ZSP_AST_BITMASK:
        walk_push(st, n->u.bitmask_decl.name);
        walk_push(st, n->u.bitmask_decl.base_type);
        walk_push_list(st, n->u.bitmask_decl.items, n->u.bitmask_decl.n_items);
        break;
    case ZSP_AST_BITMASK_ITEM:
        walk_push(st, n->u.bitmask_item.name);
        walk_push(st, n->u.bitmask_item.value_expr);
        break;
    case ZSP_AST_SUBTYPE:
        walk_push(st, n->u.subtype.target_type);
        walk_push(st, n->u.subtype.alias_name);
        break;
    case ZSP_AST_CONST:
        walk_push(st, n->u.const_decl.type);
        walk_push(st, n->u.const_decl.name);
        walk_push(st, n->u.const_decl.value_expr);
        break;
    case ZSP_AST_INSTANTIATE:
        walk_push(st, n->u.instantiate.name);
        walk_push(st, n->u.instantiate.target);
        break;
    case ZSP_AST_RULE_GROUP:
        walk_push(st, n->u.rule_group.name);
        walk_push_list(st, n->u.rule_group.rules, n->u.rule_group.n_rules);
        break;
    case ZSP_AST_RULE:
        walk_push(st, n->u.rule.name);
        walk_push(st, n->u.rule.id_expr);
        walk_push(st, n->u.rule.body_expr);
        break;
    case ZSP_AST_FUNCTION:
        walk_push(st, n->u.function_decl.return_type);
        walk_push(st, n->u.function_decl.name);
        walk_push(st, n->u.function_decl.body_expr);
        break;
    case ZSP_AST_FIELD:
        walk_push_list(st, n->u.field.annotations, n->u.field.n_annotations);
        walk_push(st, n->u.field.align_expr);
        walk_push(st, n->u.field.offset_expr);
        walk_push(st, n->u.field.type);
        walk_push(st, n->u.field.name);
        walk_push(st, n->u.field.init_expr);
        walk_push(st, n->u.field.if_clause);
        walk_push(st, n->u.field.constraint_expr);
        break;
    case ZSP_AST_PARAMETER:
        walk_push(st, n->u.parameter.type);
        walk_push(st, n->u.parameter.name);
        break;
    case ZSP_AST_TYPE_REF:
        walk_push(st, n->u.type_ref.name);
        walk_push_list(st, n->u.type_ref.template_args,
                       n->u.type_ref.n_template_args);
        walk_push_list(st, n->u.type_ref.type_args, n->u.type_ref.n_type_args);
        break;
    case ZSP_AST_TYPE_ARRAY:
        walk_push(st, n->u.type_array.element);
        walk_push(st, n->u.type_array.length_expr);
        break;
    case ZSP_AST_EXPR_BINARY:
        walk_push(st, n->u.binop.lhs);
        walk_push(st, n->u.binop.rhs);
        break;
    case ZSP_AST_EXPR_UNARY:
        walk_push(st, n->u.unop.operand);
        break;
    case ZSP_AST_EXPR_TERNARY:
        walk_push(st, n->u.ternary.cond);
        walk_push(st, n->u.ternary.then_expr);
        walk_push(st, n->u.ternary.else_expr);
        break;
    case ZSP_AST_EXPR_MEMBER:
        walk_push(st, n->u.member.lhs);
        /* The name on the RHS of a member access does not get
         * resolved against the global scope — it's looked up on
         * the LHS's type. Skip in this slice. */
        break;
    case ZSP_AST_EXPR_INDEX:
        walk_push(st, n->u.index.lhs);
        walk_push(st, n->u.index.index);
        break;
    case ZSP_AST_EXPR_CALL:
        walk_push(st, n->u.call.callee);
        walk_push_list(st, n->u.call.args, n->u.call.n_args);
        break;
    case ZSP_AST_SERVICE:
        walk_push(st, n->u.service.name);
        walk_push_list(st, n->u.service.methods, n->u.service.n_methods);
        break;
    case ZSP_AST_SERVICE_METHOD:
        walk_push(st, n->u.service_method.return_type);
        walk_push(st, n->u.service_method.name);
        walk_push(st, n->u.service_method.request_type);
        break;
    case ZSP_AST_PUBSUB:
        walk_push(st, n->u.pubsub.name);
        walk_push_list(st, n->u.pubsub.messages, n->u.pubsub.n_messages);
        break;
    case ZSP_AST_PUBSUB_MESSAGE:
        walk_push(st, n->u.pubsub_message.topic_expr);
        walk_push(st, n->u.pubsub_message.type);
        walk_push(st, n->u.pubsub_message.name);
        break;
    case ZSP_AST_SQL_TABLE:
        walk_push(st, n->u.sql_table.name);
        walk_push_list(st, n->u.sql_table.fields, n->u.sql_table.n_fields);
        walk_push_list(st, n->u.sql_table.constraints,
                       n->u.sql_table.n_constraints);
        walk_push(st, n->u.sql_table.virtual_using);
        break;
    case ZSP_AST_SQL_FIELD:
        walk_push(st, n->u.sql_field.type);
        walk_push(st, n->u.sql_field.name);
        walk_push(st, n->u.sql_field.constraint_expr);
        break;
    case ZSP_AST_SQL_DATABASE:
        walk_push(st, n->u.sql_database.name);
        walk_push_list(st, n->u.sql_database.table_refs,
                       n->u.sql_database.n_table_refs);
        break;
    case ZSP_AST_SQL_TABLE_REF:
        walk_push(st, n->u.sql_table_ref.type);
        walk_push(st, n->u.sql_table_ref.name);
        break;
    case ZSP_AST_ANNOTATION:
        walk_push(st, n->u.annotation.message);
        break;
    case ZSP_AST_TEMPLATE_PARAM:
        walk_push(st, n->u.template_param.name);
        break;
    case ZSP_AST_COMPAT_VERSION:
        walk_push(st, n->u.compat_version.version_string);
        break;
    default:
        /* Atomic literals + EXPR_IDENT have no children to push. */
        break;
    }
}

/* Walk a list of decls collecting member-name bindings into the
 * compound's local scope so that within-body references resolve.
 * Currently a no-op for member-name resolution; placeholder for
 * the next analyser slice. */
static void collect_compound_members(zsp_sema_t *s, zsp_ast_node_t *cmpd)
{
    (void)s;
    (void)cmpd;
}

static zserio_status_t resolve_pass(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    walk_stack_t st = { NULL, 0U, 0U, s->arena, false };
    walk_push(&st, unit);
    if (st.oom) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    while (st.count > 0U) {
        st.count -= 1U;
        zsp_ast_node_t *const n = st.items[st.count];
        if (n == NULL) {
            continue;
        }
        if (n->kind == ZSP_AST_TYPE_REF) {
            resolve_type_ref(s, n);
        }
        if ((n->kind == ZSP_AST_STRUCT) || (n->kind == ZSP_AST_UNION)
                || (n->kind == ZSP_AST_CHOICE)) {
            collect_compound_members(s, n);
        }
        enqueue_children(&st, n);
        if (st.oom) {
            return ZSERIO_ERR_OUT_OF_MEMORY;
        }
    }
    return ZSERIO_OK;
}

/* ----- Public API ------------------------------------------- */

zserio_status_t zsp_sema_init(zsp_sema_t           *s,
                              const zsp_source_t   *src,
                              zsp_arena_t          *arena,
                              zsp_diag_collector_t *diag)
{
    if ((s == NULL) || (src == NULL) || (arena == NULL) || (diag == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    s->source  = src;
    s->arena   = arena;
    s->diag    = diag;
    s->root    = NULL;
    s->current = NULL;
    ZSERIO_TRY(scope_new(s, NULL, &s->root));
    s->current = s->root;
    return ZSERIO_OK;
}

zserio_status_t zsp_sema_analyse_unit(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    if ((s == NULL) || (unit == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    ZSERIO_TRY(collect_top_level(s, unit));
    ZSERIO_TRY(resolve_pass(s, unit));
    return ZSERIO_OK;
}
