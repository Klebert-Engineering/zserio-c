/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#define _POSIX_C_SOURCE 200809L

#include "zsparser/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "zsparser/parser.h"

/*
 * Multi-file compilation session implementation.
 *
 * Loading flow (recursive but depth-bounded by the import
 * graph's maximum depth — schemas typically nest < 10 deep,
 * comfortably within the Q5 / Rule 17.2 deviation bound):
 *
 *   1. Resolve absolute path of the requested file.
 *   2. Skip if already loaded (path-based dedup).
 *   3. Read bytes via libc fopen/fread.
 *   4. Hand off to zsp_source_add_owned (transfers ownership
 *      of the buffer to the source manager).
 *   5. Parse to AST.
 *   6. For each import in the AST, resolve to a filesystem
 *      path and recursively load.
 *
 * Note: this module uses libc I/O directly — it's host-side
 * tooling, not on-target runtime code. The session library
 * carries this dependency intentionally.
 */

/* ----- path helpers ------------------------------------------ */

static bool file_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode) ? true : false;
}

/* Compose a candidate path: <root>/<dotted-as-slashes>.zs.
 * Returns false if buffer would overflow. */
static bool compose_import_path(char *out, size_t out_cap,
                                const char *root,
                                const char *const *segments,
                                size_t n_segments,
                                bool include_last)
{
    if (out_cap == 0U) return false;
    size_t pos = 0U;
    if (root != NULL) {
        const size_t rl = strlen(root);
        if ((rl + 2U) >= out_cap) return false;
        (void)memcpy(out, root, rl);
        pos = rl;
        if ((rl > 0U) && (root[rl - 1U] != '/')) {
            out[pos++] = '/';
        }
    }
    const size_t end = include_last ? n_segments : (n_segments - 1U);
    for (size_t i = 0U; i < end; ++i) {
        const size_t sl = strlen(segments[i]);
        if ((pos + sl + 4U) >= out_cap) return false;
        (void)memcpy(&out[pos], segments[i], sl);
        pos += sl;
        if ((i + 1U) < end) {
            out[pos++] = '/';
        }
    }
    /* Append `.zs` */
    if ((pos + 4U) >= out_cap) return false;
    out[pos++] = '.';
    out[pos++] = 'z';
    out[pos++] = 's';
    out[pos]   = '\0';
    return true;
}

/* ----- session lifecycle ------------------------------------- */

zserio_status_t zsp_session_init(zsp_session_t *s,
                                 const char *const *roots,
                                 size_t n_roots)
{
    if (s == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    s->alloc = zserio_allocator_libc();
    ZSERIO_TRY(zsp_source_init(&s->source, &s->alloc));
    ZSERIO_TRY(zsp_arena_init(&s->arena, &s->alloc));
    ZSERIO_TRY(zsp_diag_init(&s->diag, &s->alloc, 256U));
    s->roots       = roots;
    s->n_roots     = n_roots;
    s->files       = NULL;
    s->n_files     = 0U;
    s->files_cap   = 0U;
    s->sema_inited = false;
    return ZSERIO_OK;
}

void zsp_session_dispose(zsp_session_t *s)
{
    if (s == NULL) return;
    for (size_t i = 0U; i < s->n_files; ++i) {
        free(s->files[i].path);
    }
    free(s->files);
    zsp_diag_dispose(&s->diag);
    zsp_arena_dispose(&s->arena);
    zsp_source_dispose(&s->source);
    s->files       = NULL;
    s->n_files     = 0U;
    s->files_cap   = 0U;
    s->sema_inited = false;
}

/* ----- file lookup ------------------------------------------- */

/* Return true if `path` is already loaded (linear search). */
static bool already_loaded(const zsp_session_t *s, const char *path)
{
    for (size_t i = 0U; i < s->n_files; ++i) {
        if (strcmp(s->files[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static zserio_status_t append_file_record(zsp_session_t *s,
                                          const char *path,
                                          zsp_ast_node_t *unit)
{
    if (s->n_files == s->files_cap) {
        const size_t new_cap = (s->files_cap == 0U) ? 16U
                                                    : (s->files_cap * 2U);
        void *const grown = realloc(s->files,
                                    new_cap * sizeof(*s->files));
        if (grown == NULL) {
            return ZSERIO_ERR_OUT_OF_MEMORY;
        }
        s->files     = grown;
        s->files_cap = new_cap;
    }
    s->files[s->n_files].path = strdup(path);
    if (s->files[s->n_files].path == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    s->files[s->n_files].unit = unit;
    s->n_files += 1U;
    return ZSERIO_OK;
}

/* ----- File I/O ----------------------------------------------- */

static uint8_t *slurp_file(const char *path, size_t *out_len,
                           const zserio_allocator_t *alloc,
                           size_t *out_alloc_size)
{
    FILE *const f = fopen(path, "rb");
    if (f == NULL) return NULL;
    (void)fseek(f, 0L, SEEK_END);
    const long sz = ftell(f);
    if ((sz < 0L) || (fseek(f, 0L, SEEK_SET) != 0)) {
        (void)fclose(f);
        return NULL;
    }
    const size_t alloc_size = (size_t)sz + 1U;
    uint8_t *const buf = (uint8_t *)alloc->vtable->alloc(
            alloc->userdata, alloc_size, 1U);
    if (buf == NULL) {
        (void)fclose(f);
        return NULL;
    }
    const size_t got = fread(buf, 1U, (size_t)sz, f);
    (void)fclose(f);
    if (got != (size_t)sz) {
        alloc->vtable->free(alloc->userdata, buf, alloc_size, 1U);
        return NULL;
    }
    buf[sz] = (uint8_t)'\0';
    *out_len = (size_t)sz;
    *out_alloc_size = alloc_size;
    return buf;
}

/* ----- Import resolution ------------------------------------- */

/* Extract package segments from a dotted name as a list of
 * malloc'd C-strings. Caller frees each. */
static zserio_status_t extract_segments(const zsp_source_t *src,
                                        const zsp_ast_node_t *name,
                                        char ***out_segments,
                                        size_t *out_n)
{
    *out_segments = NULL;
    *out_n        = 0U;
    if (name == NULL) {
        return ZSERIO_OK;
    }
    /* Either a single IDENT or a DOTTED_NAME. */
    zsp_ast_node_t * const *parts = NULL;
    size_t n_parts = 0U;
    zsp_ast_node_t *single[1] = { NULL };
    if (name->kind == ZSP_AST_DOTTED_NAME) {
        parts   = name->u.dotted.parts;
        n_parts = name->u.dotted.n_parts;
    }
    else if (name->kind == ZSP_AST_EXPR_IDENT) {
        /* Non-const cast is safe here: we only read from `parts`
         * in the loop below. The const-correct shape would
         * require a separate code path for the single-IDENT case. */
        single[0] = (zsp_ast_node_t *)(uintptr_t)name;
        parts     = single;
        n_parts   = 1U;
    }
    else {
        return ZSERIO_OK;
    }
    char **out = (char **)malloc(n_parts * sizeof(char *));
    if (out == NULL) return ZSERIO_ERR_OUT_OF_MEMORY;
    for (size_t i = 0U; i < n_parts; ++i) {
        const zsp_file_t *f = NULL;
        if (!zsp_source_get(src, parts[i]->file_id, &f)) {
            out[i] = strdup("");
            continue;
        }
        const size_t len = (size_t)parts[i]->u.ident.span_len;
        out[i] = (char *)malloc(len + 1U);
        if (out[i] == NULL) {
            for (size_t k = 0U; k < i; ++k) free(out[k]);
            free(out);
            return ZSERIO_ERR_OUT_OF_MEMORY;
        }
        (void)memcpy(out[i], &f->bytes[parts[i]->u.ident.span_off], len);
        out[i][len] = '\0';
    }
    *out_segments = out;
    *out_n        = n_parts;
    return ZSERIO_OK;
}

static void free_segments(char **segs, size_t n)
{
    if (segs == NULL) return;
    for (size_t i = 0U; i < n; ++i) free(segs[i]);
    free(segs);
}

/* Resolve an import declaration to a filesystem path by trying
 * candidates in order. Returns ZSERIO_OK with *out_path set on
 * success; ZSERIO_ERR_INVALID_ARGUMENT if the import is
 * malformed; or a non-OK status with *out_path[0]='\0' if the
 * file cannot be found. */
static zserio_status_t resolve_import_path(const zsp_session_t *s,
                                           const zsp_ast_node_t *imp,
                                           char *out_path, size_t out_cap)
{
    out_path[0] = '\0';
    char **segs = NULL;
    size_t n_segs = 0U;
    ZSERIO_TRY(extract_segments(&s->source, imp->u.import.name,
                                &segs, &n_segs));
    if (n_segs == 0U) {
        free_segments(segs, n_segs);
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    const char *const *seg_strs = (const char *const *)segs;

    /* Try each root in order. */
    for (size_t r = 0U; r < (s->n_roots + 1U); ++r) {
        const char *root = (r < s->n_roots) ? s->roots[r] : NULL;
        /* Candidate 1: full path treated as package. */
        if (compose_import_path(out_path, out_cap, root,
                                seg_strs, n_segs, true)
                && file_exists(out_path)) {
            free_segments(segs, n_segs);
            return ZSERIO_OK;
        }
        /* Candidate 2: drop last segment, treat as symbol-from-package. */
        if (!imp->u.import.wildcard && (n_segs > 1U)) {
            if (compose_import_path(out_path, out_cap, root,
                                    seg_strs, n_segs, false)
                    && file_exists(out_path)) {
                free_segments(segs, n_segs);
                return ZSERIO_OK;
            }
        }
    }
    free_segments(segs, n_segs);
    out_path[0] = '\0';
    return ZSERIO_ERR_END_OF_STREAM;   /* not-found sentinel */
}

/* ----- Recursive load ---------------------------------------- */

zserio_status_t zsp_session_load(zsp_session_t   *s,
                                 const char      *path,
                                 zsp_ast_node_t **out_unit)
{
    if ((s == NULL) || (path == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* Normalise: resolve to absolute path. For the MVP we just
     * use the path as-given; callers pass absolute or correctly
     * relative paths. */
    if (already_loaded(s, path)) {
        /* Return the previously-loaded unit. */
        for (size_t i = 0U; i < s->n_files; ++i) {
            if (strcmp(s->files[i].path, path) == 0) {
                if (out_unit != NULL) *out_unit = s->files[i].unit;
                return ZSERIO_OK;
            }
        }
    }

    size_t len = 0U;
    size_t alloc_size = 0U;
    uint8_t *const bytes = slurp_file(path, &len, &s->alloc, &alloc_size);
    if (bytes == NULL) {
        return ZSERIO_ERR_END_OF_STREAM;   /* not-found sentinel */
    }

    /* Path is owned by us; copy via allocator so source manager
     * has matching ownership. */
    const size_t path_len  = strlen(path);
    char *const path_copy = (char *)s->alloc.vtable->alloc(
            s->alloc.userdata, path_len + 1U, 1U);
    if (path_copy == NULL) {
        s->alloc.vtable->free(s->alloc.userdata, bytes, alloc_size, 1U);
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    (void)memcpy(path_copy, path, path_len);
    path_copy[path_len] = '\0';

    zsp_file_id_t fid;
    if (zsp_source_add_owned(&s->source,
                             path_copy, path_len, path_len + 1U,
                             bytes, len, alloc_size,
                             &fid) != ZSERIO_OK) {
        s->alloc.vtable->free(s->alloc.userdata, bytes, alloc_size, 1U);
        s->alloc.vtable->free(s->alloc.userdata, path_copy, path_len + 1U, 1U);
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }

    zsp_ast_node_t *unit = NULL;
    ZSERIO_TRY(zsp_parse_unit(&s->source, fid, &s->arena, &s->diag, &unit));

    ZSERIO_TRY(append_file_record(s, path, unit));
    if (out_unit != NULL) {
        *out_unit = unit;
    }

    /* Recurse into imports. */
    for (size_t i = 0U; (unit != NULL) && (i < unit->u.unit.n_imports); ++i) {
        char dep_path[2048];
        const zserio_status_t st = resolve_import_path(
                s, unit->u.unit.imports[i], dep_path, sizeof(dep_path));
        if (st == ZSERIO_OK) {
            (void)zsp_session_load(s, dep_path, NULL);
            /* Non-OK return on dependency load is recorded as a
             * diagnostic; analysis later emits "undeclared
             * reference" for anything that wasn't found. */
        }
        /* Missing dependency files are not fatal here — they
         * surface as unresolved type-refs at analyse time. */
    }
    return ZSERIO_OK;
}

zserio_status_t zsp_session_analyse(zsp_session_t *s)
{
    if (s == NULL) return ZSERIO_ERR_INVALID_ARGUMENT;
    if (!s->sema_inited) {
        ZSERIO_TRY(zsp_sema_init(&s->sema, &s->source, &s->arena, &s->diag));
        s->sema_inited = true;
    }
    /* Phase 1: register every loaded unit's top-level decls. */
    for (size_t i = 0U; i < s->n_files; ++i) {
        if (s->files[i].unit != NULL) {
            ZSERIO_TRY(zsp_sema_register_unit(&s->sema, s->files[i].unit));
        }
    }
    /* Phase 2: resolve + const-eval each unit. */
    for (size_t i = 0U; i < s->n_files; ++i) {
        if (s->files[i].unit != NULL) {
            ZSERIO_TRY(zsp_sema_resolve_unit(&s->sema, s->files[i].unit));
        }
    }
    /* Phase 3: template instantiation across all units. Runs
     * after all units are resolved so cloning sees fully-
     * resolved template bodies regardless of unit order. */
    for (size_t i = 0U; i < s->n_files; ++i) {
        if (s->files[i].unit != NULL) {
            ZSERIO_TRY(zsp_sema_instantiate_templates(
                    &s->sema, s->files[i].unit));
        }
    }
    return ZSERIO_OK;
}

/* Build a fresh IDENT node referring to `name` (length `len`),
 * registering a borrowed source view so diagnostics + ident_span
 * read consistent bytes. The buffer is copied into the session
 * arena and outlives any caller scope. */
static zsp_ast_node_t *prefix_make_ident(zsp_session_t *s,
                                         const char *name, size_t len)
{
    char *buf = (char *)zsp_arena_alloc(&s->arena, len + 1U, 1U);
    if (buf == NULL) return NULL;
    (void)memcpy(buf, name, len);
    buf[len] = '\0';
    zsp_file_id_t fid = (zsp_file_id_t)0;
    if (zsp_source_add_borrowed(&s->source, buf, len,
                                (const uint8_t *)buf, len,
                                &fid) != ZSERIO_OK) {
        return NULL;
    }
    zsp_ast_node_t *n = (zsp_ast_node_t *)zsp_arena_alloc(
            &s->arena, sizeof(zsp_ast_node_t), sizeof(void *));
    if (n == NULL) return NULL;
    (void)memset(n, 0, sizeof(*n));
    n->kind             = ZSP_AST_EXPR_IDENT;
    n->file_id          = fid;
    n->offset           = 0U;
    n->length           = (uint32_t)len;
    n->u.ident.span_off = 0U;
    n->u.ident.span_len = (uint32_t)len;
    return n;
}

zserio_status_t zsp_session_set_top_level_package(zsp_session_t *s,
                                                  const char    *prefix)
{
    if (s == NULL) return ZSERIO_ERR_INVALID_ARGUMENT;
    if ((prefix == NULL) || (*prefix == '\0')) return ZSERIO_OK;

    /* Split prefix on `.`. Cap at 16 segments — beyond that the
     * configuration is almost certainly a mistake; the cap mirrors
     * what the CLI accepted historically. */
    zsp_ast_node_t *prefix_parts[16];
    size_t n_prefix = 0U;
    const char *p = prefix;
    while ((*p != '\0')
            && (n_prefix < (sizeof(prefix_parts) / sizeof(prefix_parts[0])))) {
        const char *q = p;
        while ((*q != '\0') && (*q != '.')) ++q;
        zsp_ast_node_t *id = prefix_make_ident(s, p, (size_t)(q - p));
        if (id == NULL) return ZSERIO_ERR_OUT_OF_MEMORY;
        prefix_parts[n_prefix++] = id;
        if (*q == '\0') break;
        p = q + 1;
    }

    for (size_t i = 0U; i < s->n_files; ++i) {
        zsp_ast_node_t *unit = s->files[i].unit;
        if ((unit == NULL) || (unit->kind != ZSP_AST_UNIT)) continue;
        zsp_ast_node_t *pkg = unit->u.unit.package;
        if (pkg == NULL) continue;
        zsp_ast_node_t *pname = pkg->u.package.name;
        if (pname == NULL) continue;

        zsp_ast_node_t **old_parts = NULL;
        size_t           n_old     = 0U;
        if (pname->kind == ZSP_AST_DOTTED_NAME) {
            old_parts = pname->u.dotted.parts;
            n_old     = pname->u.dotted.n_parts;
        }
        else if (pname->kind == ZSP_AST_EXPR_IDENT) {
            old_parts = (zsp_ast_node_t **)zsp_arena_alloc(
                    &s->arena, sizeof(zsp_ast_node_t *), sizeof(void *));
            if (old_parts == NULL) return ZSERIO_ERR_OUT_OF_MEMORY;
            old_parts[0] = pname;
            n_old = 1U;
        }

        const size_t n_total = n_prefix + n_old;
        zsp_ast_node_t **parts = (zsp_ast_node_t **)zsp_arena_alloc(
                &s->arena, n_total * sizeof(zsp_ast_node_t *),
                sizeof(void *));
        if (parts == NULL) return ZSERIO_ERR_OUT_OF_MEMORY;
        (void)memcpy(parts, prefix_parts,
                     n_prefix * sizeof(zsp_ast_node_t *));
        (void)memcpy(parts + n_prefix, old_parts,
                     n_old * sizeof(zsp_ast_node_t *));

        zsp_ast_node_t *dotted = (zsp_ast_node_t *)zsp_arena_alloc(
                &s->arena, sizeof(zsp_ast_node_t), sizeof(void *));
        if (dotted == NULL) return ZSERIO_ERR_OUT_OF_MEMORY;
        (void)memset(dotted, 0, sizeof(*dotted));
        dotted->kind             = ZSP_AST_DOTTED_NAME;
        dotted->file_id          = pname->file_id;
        dotted->offset           = pname->offset;
        dotted->length           = pname->length;
        dotted->u.dotted.parts   = parts;
        dotted->u.dotted.n_parts = n_total;
        pkg->u.package.name = dotted;
    }
    return ZSERIO_OK;
}
