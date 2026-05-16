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

/* Bind a single field/parameter/template-param's name into the
 * given scope. Silently skips nodes we can't extract a name
 * from. Used during compound-scope construction. */
static zserio_status_t bind_member(zsp_sema_t *s, zsp_sema_scope_t *sc,
                                   zsp_ast_node_t *member)
{
    if (member == NULL) {
        return ZSERIO_OK;
    }
    const zsp_ast_node_t *name_node = NULL;
    switch (member->kind) {
    case ZSP_AST_FIELD:           name_node = member->u.field.name;            break;
    case ZSP_AST_PARAMETER:       name_node = member->u.parameter.name;        break;
    case ZSP_AST_TEMPLATE_PARAM:  name_node = member->u.template_param.name;   break;
    case ZSP_AST_FUNCTION:        name_node = member->u.function_decl.name;    break;
    case ZSP_AST_ENUM_ITEM:       name_node = member->u.enum_item.name;        break;
    case ZSP_AST_BITMASK_ITEM:    name_node = member->u.bitmask_item.name;     break;
    case ZSP_AST_SQL_FIELD:       name_node = member->u.sql_field.name;        break;
    case ZSP_AST_SQL_TABLE_REF:   name_node = member->u.sql_table_ref.name;    break;
    case ZSP_AST_SERVICE_METHOD:  name_node = member->u.service_method.name;   break;
    case ZSP_AST_PUBSUB_MESSAGE:  name_node = member->u.pubsub_message.name;   break;
    case ZSP_AST_CHOICE_CASE:     name_node = (member->u.choice_case.field != NULL)
                                              ? member->u.choice_case.field->u.field.name
                                              : NULL; break;
    case ZSP_AST_CHOICE_DEFAULT:  name_node = (member->u.choice_default.field != NULL)
                                              ? member->u.choice_default.field->u.field.name
                                              : NULL; break;
    default:                      break;
    }
    if (name_node == NULL) {
        return ZSERIO_OK;
    }
    const uint8_t *bytes = NULL;
    size_t         len   = 0U;
    if (!extract_simple_name(s, name_node, &bytes, &len)) {
        return ZSERIO_OK;
    }
    /* Per-compound scope: duplicate field names are a hard error
     * since members share a single namespace. */
    for (size_t i = 0U; i < sc->count; ++i) {
        const zsp_sema_binding_t *const b = &sc->bindings[i];
        if ((b->name_len == len) && (memcmp(b->name, bytes, len) == 0)) {
            (void)zsp_diag_emit_lit(s->diag, ZSP_SEV_ERROR,
                                    member->file_id, member->offset,
                                    "duplicate member name in compound type");
            return ZSERIO_OK;
        }
    }
    ZSERIO_TRY(scope_grow(s, sc));
    sc->bindings[sc->count].name     = bytes;
    sc->bindings[sc->count].name_len = len;
    sc->bindings[sc->count].decl     = member;
    sc->count += 1U;
    return ZSERIO_OK;
}

/* Build a fresh scope for a compound's body and bind its
 * template params + parameters + members. Returns the new scope
 * (which is also pushed as the current). */
static zserio_status_t enter_compound_scope(zsp_sema_t *s,
                                            zsp_ast_node_t *cmpd,
                                            zsp_sema_scope_t **out_scope)
{
    zsp_sema_scope_t *scope = NULL;
    ZSERIO_TRY(scope_new(s, s->current, &scope));

    if ((cmpd->kind == ZSP_AST_STRUCT) || (cmpd->kind == ZSP_AST_UNION)) {
        for (size_t i = 0U; i < cmpd->u.compound.n_template_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope,
                                   cmpd->u.compound.template_params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.compound.n_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.compound.params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.compound.members[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_CHOICE) {
        for (size_t i = 0U; i < cmpd->u.choice.n_template_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope,
                                   cmpd->u.choice.template_params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.choice.n_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.choice.params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.choice.n_cases; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.choice.cases[i]));
        }
        if (cmpd->u.choice.default_case != NULL) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.choice.default_case));
        }
    }
    else if (cmpd->kind == ZSP_AST_ENUM) {
        for (size_t i = 0U; i < cmpd->u.enum_decl.n_items; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.enum_decl.items[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_BITMASK) {
        for (size_t i = 0U; i < cmpd->u.bitmask_decl.n_items; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.bitmask_decl.items[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_SQL_TABLE) {
        for (size_t i = 0U; i < cmpd->u.sql_table.n_fields; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.sql_table.fields[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_SQL_DATABASE) {
        for (size_t i = 0U; i < cmpd->u.sql_database.n_table_refs; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.sql_database.table_refs[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_SERVICE) {
        for (size_t i = 0U; i < cmpd->u.service.n_methods; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.service.methods[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_PUBSUB) {
        for (size_t i = 0U; i < cmpd->u.pubsub.n_messages; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.pubsub.messages[i]));
        }
    }

    s->current = scope;
    *out_scope = scope;
    return ZSERIO_OK;
}

static bool is_scope_owner(zsp_ast_kind_t k)
{
    return (k == ZSP_AST_STRUCT) || (k == ZSP_AST_UNION) || (k == ZSP_AST_CHOICE)
        || (k == ZSP_AST_ENUM) || (k == ZSP_AST_BITMASK)
        || (k == ZSP_AST_SQL_TABLE) || (k == ZSP_AST_SQL_DATABASE)
        || (k == ZSP_AST_SERVICE) || (k == ZSP_AST_PUBSUB);
}

/* Resolve EXPR_IDENT against the lexical scope chain. */
static void resolve_expr_ident(zsp_sema_t *s, zsp_ast_node_t *e)
{
    if (e->resolved != NULL) {
        return;
    }
    const uint8_t *const src = source_bytes(s, e->file_id);
    if (src == NULL) {
        return;
    }
    const uint8_t *const name = &src[e->u.ident.span_off];
    const size_t         len  = (size_t)e->u.ident.span_len;
    /* `@index` and a few other pseudo-idents start with '@' and
     * are not in any scope. Skip those. */
    if ((len > 0U) && (name[0] == (uint8_t)'@')) {
        return;
    }
    zsp_ast_node_t *const decl = zsp_sema_scope_find(s->current, name, len);
    if (decl == NULL) {
        /* Identifier resolution is best-effort: it may legitimately
         * fail for member-access RHS, package-qualified references,
         * or template-parameter uses inside instantiations. We
         * emit no diagnostic here; only TYPE_REF (which definitely
         * names a type) gets the strict-error treatment. */
        return;
    }
    e->resolved = decl;
}

/* Resolve every TYPE_REF + EXPR_IDENT under `n`, with scope
 * push/pop around compound bodies. Recursive — bounded by AST
 * nesting depth, covered by the Q5 / Rule 17.2 deviation. */
static zserio_status_t resolve_walk(zsp_sema_t *s, zsp_ast_node_t *n)
{
    if (n == NULL) {
        return ZSERIO_OK;
    }

    zsp_sema_scope_t *saved = NULL;
    if (is_scope_owner(n->kind)) {
        saved = s->current;
        zsp_sema_scope_t *new_scope = NULL;
        ZSERIO_TRY(enter_compound_scope(s, n, &new_scope));
        (void)new_scope;
    }

    if (n->kind == ZSP_AST_TYPE_REF) {
        resolve_type_ref(s, n);
    }
    else if (n->kind == ZSP_AST_EXPR_IDENT) {
        resolve_expr_ident(s, n);
    }

    /* Walk children. We can't reuse the iterative enqueue_children
     * verbatim here because we need recursive descent — recurse
     * into each child to maintain scope discipline. */
    walk_stack_t st = { NULL, 0U, 0U, s->arena, false };
    enqueue_children(&st, n);
    if (st.oom) {
        if (saved != NULL) {
            s->current = saved;
        }
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    for (size_t i = 0U; i < st.count; ++i) {
        ZSERIO_TRY(resolve_walk(s, st.items[i]));
    }

    if (saved != NULL) {
        s->current = saved;
    }
    return ZSERIO_OK;
}

static zserio_status_t resolve_pass(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    return resolve_walk(s, unit);
}

/* ============================================================ */
/* Constant evaluation                                           */
/* ============================================================ */

static zsp_sema_const_t CYCLE_SENTINEL;

const zsp_sema_const_t *zsp_sema_const_cycle_marker(void)
{
    return &CYCLE_SENTINEL;
}

static zsp_sema_const_t *alloc_const(zsp_sema_t *s, zsp_const_kind_t k)
{
    zsp_sema_const_t *const c = (zsp_sema_const_t *)zsp_arena_alloc(
            s->arena, sizeof(zsp_sema_const_t), sizeof(void *));
    if (c == NULL) {
        return NULL;
    }
    c->kind = k;
    return c;
}

static const zsp_sema_const_t *eval_uint(zsp_sema_t *s, uint64_t v)
{
    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_UINT);
    if (c == NULL) return NULL;
    c->v.u = v;
    return c;
}

static const zsp_sema_const_t *eval_bool(zsp_sema_t *s, bool v)
{
    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_BOOL);
    if (c == NULL) return NULL;
    c->v.b = v;
    return c;
}

static const zsp_sema_const_t *eval_float(zsp_sema_t *s, double v)
{
    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_FLOAT);
    if (c == NULL) return NULL;
    c->v.f = v;
    return c;
}

static uint64_t to_u64(const zsp_sema_const_t *c)
{
    if (c->kind == ZSP_CONST_BOOL) return c->v.b ? 1U : 0U;
    if (c->kind == ZSP_CONST_INT)  return (uint64_t)c->v.i;
    return c->v.u;
}

static const zsp_sema_const_t *eval_binop(zsp_sema_t *s, zsp_op_code_t op,
                                          const zsp_sema_const_t *l,
                                          const zsp_sema_const_t *r)
{
    if ((l->kind == ZSP_CONST_FLOAT) || (r->kind == ZSP_CONST_FLOAT)) {
        const double lv = (l->kind == ZSP_CONST_FLOAT) ? l->v.f : (double)to_u64(l);
        const double rv = (r->kind == ZSP_CONST_FLOAT) ? r->v.f : (double)to_u64(r);
        /* Float division-by-zero check: compare via bit pattern
         * rather than `==` to keep -Wfloat-equal clean. The
         * runtime's own `zserio_float_eq_bits` helper is the
         * reference for this idiom (REQ-FS-002 spirit). */
        const uint64_t rv_bits = ((const uint64_t *)&rv)[0];
        const uint64_t zero_bits = 0U;
        switch (op) {
        case ZSP_OP_ADD: return eval_float(s, lv + rv);
        case ZSP_OP_SUB: return eval_float(s, lv - rv);
        case ZSP_OP_MUL: return eval_float(s, lv * rv);
        case ZSP_OP_DIV: return (rv_bits != zero_bits) ? eval_float(s, lv / rv) : NULL;
        case ZSP_OP_EQ:  return eval_bool(s, !(lv <  rv) && !(lv >  rv));
        case ZSP_OP_NEQ: return eval_bool(s, (lv <  rv) ||  (lv >  rv));
        case ZSP_OP_LT:  return eval_bool(s, lv <  rv);
        case ZSP_OP_LE:  return eval_bool(s, lv <= rv);
        case ZSP_OP_GT:  return eval_bool(s, lv >  rv);
        case ZSP_OP_GE:  return eval_bool(s, lv >= rv);
        default:         return NULL;
        }
    }
    const uint64_t lu = to_u64(l);
    const uint64_t ru = to_u64(r);
    switch (op) {
    case ZSP_OP_ADD:         return eval_uint(s, lu + ru);
    case ZSP_OP_SUB:         return eval_uint(s, lu - ru);
    case ZSP_OP_MUL:         return eval_uint(s, lu * ru);
    case ZSP_OP_DIV:         return (ru != 0U) ? eval_uint(s, lu / ru) : NULL;
    case ZSP_OP_MOD:         return (ru != 0U) ? eval_uint(s, lu % ru) : NULL;
    case ZSP_OP_LSHIFT:      return eval_uint(s, lu << (ru & 63U));
    case ZSP_OP_RSHIFT:      return eval_uint(s, lu >> (ru & 63U));
    case ZSP_OP_BIT_AND:     return eval_uint(s, lu & ru);
    case ZSP_OP_BIT_OR:      return eval_uint(s, lu | ru);
    case ZSP_OP_BIT_XOR:     return eval_uint(s, lu ^ ru);
    case ZSP_OP_LOGICAL_AND: return eval_bool(s, (lu != 0U) && (ru != 0U));
    case ZSP_OP_LOGICAL_OR:  return eval_bool(s, (lu != 0U) || (ru != 0U));
    case ZSP_OP_EQ:          return eval_bool(s, lu == ru);
    case ZSP_OP_NEQ:         return eval_bool(s, lu != ru);
    case ZSP_OP_LT:          return eval_bool(s, lu <  ru);
    case ZSP_OP_LE:          return eval_bool(s, lu <= ru);
    case ZSP_OP_GT:          return eval_bool(s, lu >  ru);
    case ZSP_OP_GE:          return eval_bool(s, lu >= ru);
    default:                 return NULL;
    }
}

static const zsp_sema_const_t *eval_unop(zsp_sema_t *s, zsp_op_code_t op,
                                         const zsp_sema_const_t *o)
{
    switch (op) {
    case ZSP_OP_NEG:
        if (o->kind == ZSP_CONST_FLOAT) {
            return eval_float(s, -o->v.f);
        }
        return eval_uint(s, (uint64_t)(-(int64_t)to_u64(o)));
    case ZSP_OP_BIT_NOT:
        return eval_uint(s, ~to_u64(o));
    case ZSP_OP_LOGICAL_NOT:
        return eval_bool(s, to_u64(o) == 0U);
    default:
        return NULL;
    }
}

const zsp_sema_const_t *zsp_sema_eval(zsp_sema_t *s, zsp_ast_node_t *expr)
{
    if ((s == NULL) || (expr == NULL)) {
        return NULL;
    }
    if (expr->computed != NULL) {
        if (expr->computed == &CYCLE_SENTINEL) {
            (void)zsp_diag_emit_lit(s->diag, ZSP_SEV_ERROR,
                                    expr->file_id, expr->offset,
                                    "cyclic constant expression");
            return NULL;
        }
        return (const zsp_sema_const_t *)expr->computed;
    }

    /* Mark in-flight to detect cycles. */
    expr->computed = &CYCLE_SENTINEL;

    const zsp_sema_const_t *result = NULL;
    switch (expr->kind) {
    case ZSP_AST_EXPR_INT_LIT:
        result = eval_uint(s, expr->u.int_lit.value);
        break;
    case ZSP_AST_EXPR_BOOL_LIT:
        result = eval_bool(s, expr->u.bool_lit.value);
        break;
    case ZSP_AST_EXPR_FLOAT_LIT:
        result = eval_float(s, expr->u.float_lit.value);
        break;
    case ZSP_AST_EXPR_IDENT:
    {
        zsp_ast_node_t *const d = expr->resolved;
        if (d == NULL) {
            result = NULL;
        }
        else if (d->kind == ZSP_AST_ENUM_ITEM) {
            /* Enum item: evaluate its value_expr if present;
             * otherwise use the implicit "previous value + 1"
             * computation handled in compute_enum_values. */
            if (d->u.enum_item.value_expr != NULL) {
                result = zsp_sema_eval(s, d->u.enum_item.value_expr);
            }
            else if (d->computed != NULL) {
                result = (const zsp_sema_const_t *)d->computed;
            }
        }
        else if (d->kind == ZSP_AST_BITMASK_ITEM) {
            if (d->u.bitmask_item.value_expr != NULL) {
                result = zsp_sema_eval(s, d->u.bitmask_item.value_expr);
            }
            else if (d->computed != NULL) {
                result = (const zsp_sema_const_t *)d->computed;
            }
        }
        else if (d->kind == ZSP_AST_CONST) {
            if (d->u.const_decl.value_expr != NULL) {
                result = zsp_sema_eval(s, d->u.const_decl.value_expr);
            }
        }
        break;
    }
    case ZSP_AST_EXPR_BINARY:
    {
        const zsp_sema_const_t *const l = zsp_sema_eval(s, expr->u.binop.lhs);
        const zsp_sema_const_t *const r = zsp_sema_eval(s, expr->u.binop.rhs);
        if ((l != NULL) && (r != NULL)) {
            result = eval_binop(s, expr->u.binop.op, l, r);
        }
        break;
    }
    case ZSP_AST_EXPR_UNARY:
    {
        const zsp_sema_const_t *const o = zsp_sema_eval(s, expr->u.unop.operand);
        if (o != NULL) {
            result = eval_unop(s, expr->u.unop.op, o);
        }
        break;
    }
    case ZSP_AST_EXPR_TERNARY:
    {
        const zsp_sema_const_t *const c = zsp_sema_eval(s, expr->u.ternary.cond);
        if (c != NULL) {
            const bool cond = (to_u64(c) != 0U);
            result = zsp_sema_eval(s, cond ? expr->u.ternary.then_expr
                                           : expr->u.ternary.else_expr);
        }
        break;
    }
    default:
        result = NULL;
        break;
    }

    /* Replace the in-flight sentinel with the real result (or
     * clear it if non-constant). */
    if (result != NULL) {
        expr->computed = (void *)(uintptr_t)result;
    }
    else {
        expr->computed = NULL;
    }
    return result;
}

/* Walk all enum + bitmask + const decls and pre-evaluate item
 * values so that downstream lookups via EXPR_IDENT find them. */
static zserio_status_t evaluate_known_constants(zsp_sema_t *s,
                                                zsp_ast_node_t *unit)
{
    if (unit->kind != ZSP_AST_UNIT) {
        return ZSERIO_OK;
    }
    for (size_t i = 0U; i < unit->u.unit.n_decls; ++i) {
        zsp_ast_node_t *const d = unit->u.unit.decls[i];
        if (d == NULL) continue;
        if (d->kind == ZSP_AST_ENUM) {
            uint64_t prev = 0U;
            bool     first = true;
            for (size_t j = 0U; j < d->u.enum_decl.n_items; ++j) {
                zsp_ast_node_t *const item = d->u.enum_decl.items[j];
                if (item->u.enum_item.value_expr != NULL) {
                    const zsp_sema_const_t *const v = zsp_sema_eval(
                            s, item->u.enum_item.value_expr);
                    if (v != NULL) {
                        prev = to_u64(v);
                        first = false;
                    }
                    item->computed = (void *)(uintptr_t)v;
                }
                else {
                    /* Implicit: previous + 1, or 0 for the first. */
                    const uint64_t v = first ? 0U : (prev + 1U);
                    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_UINT);
                    if (c != NULL) {
                        c->v.u = v;
                        item->computed = c;
                    }
                    prev  = v;
                    first = false;
                }
            }
        }
        else if (d->kind == ZSP_AST_BITMASK) {
            uint64_t prev = 0U;
            bool     first = true;
            for (size_t j = 0U; j < d->u.bitmask_decl.n_items; ++j) {
                zsp_ast_node_t *const item = d->u.bitmask_decl.items[j];
                if (item->u.bitmask_item.value_expr != NULL) {
                    const zsp_sema_const_t *const v = zsp_sema_eval(
                            s, item->u.bitmask_item.value_expr);
                    if (v != NULL) {
                        prev = to_u64(v);
                        first = false;
                    }
                    item->computed = (void *)(uintptr_t)v;
                }
                else {
                    /* Bitmask implicit: previous << 1, or 1 for first. */
                    const uint64_t v = first ? 1U : (prev << 1U);
                    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_UINT);
                    if (c != NULL) {
                        c->v.u = v;
                        item->computed = c;
                    }
                    prev  = v;
                    first = false;
                }
            }
        }
        else if (d->kind == ZSP_AST_CONST) {
            if (d->u.const_decl.value_expr != NULL) {
                (void)zsp_sema_eval(s, d->u.const_decl.value_expr);
            }
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
    ZSERIO_TRY(evaluate_known_constants(s, unit));
    return ZSERIO_OK;
}
