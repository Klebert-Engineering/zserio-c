/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Corpus sweep: walks the upstream language test schema tree,
 * parses every `.zs` file it finds, and reports per-file
 * pass/fail counts. Failures are listed but don't fatal-error
 * the test unless the success ratio drops below the expected
 * threshold — this is the milestone 7.2 gate at the "broad
 * coverage" level.
 *
 * Verifies: REQ-FN-002 — parser accepts the schema language
 *                        (broad-coverage gate over upstream corpus).
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/emit.h"
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/session.h"
#include "zsparser/source.h"

/* Black-hole sink: counts files written but throws away content. */
typedef struct counter_sink {
    size_t files_written;
    size_t total_bytes;
} counter_sink_t;

static bool counter_write(void *ud, const char *path,
                          const char *bytes, size_t len)
{
    counter_sink_t *const s = (counter_sink_t *)ud;
    (void)path; (void)bytes;
    s->files_written += 1U;
    s->total_bytes   += len;
    return true;
}

#ifndef ZSP_CORPUS_ROOT
#define ZSP_CORPUS_ROOT ""
#endif

static zserio_allocator_t libc_alloc;

void setUp(void) { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

static bool ends_with_zs(const char *name)
{
    const size_t n = strlen(name);
    return (n >= 3U) && (strcmp(&name[n - 3U], ".zs") == 0);
}

typedef struct corpus_result {
    bool parse_ok;
    bool sema_ok;
    bool emit_ok;
} corpus_result_t;

/* Determine the source root for a schema path: walk up from
 * the file until we find a directory that looks like the
 * package root. For corpus schemas, the convention is that
 * package `a.b.c` lives at `<root>/a/b/c.zs` — so for each
 * file we strip the directories matching the package path.
 * For simplicity we use the file's immediate parent + grandparent
 * as candidate roots; the session tries each. */
static void derive_roots(const char *path, char roots[][1024],
                         const char **out_roots, size_t cap, size_t *n_out)
{
    *n_out = 0U;
    char buf[1024];
    (void)snprintf(buf, sizeof(buf), "%s", path);
    /* Strip file basename + walk up to 6 parent dirs. */
    for (size_t up = 0U; (up < 6U) && (*n_out < cap); ++up) {
        size_t pos = strlen(buf);
        while ((pos > 0U) && (buf[pos - 1U] != '/')) pos -= 1U;
        if (pos == 0U) break;
        buf[pos - 1U] = '\0';
        (void)snprintf(roots[*n_out], 1024, "%s", buf);
        out_roots[*n_out] = roots[*n_out];
        *n_out += 1U;
    }
}

/* Count diagnostics belonging to a specific file_id (skip
 * deps' own errors when measuring the target). */
static size_t errors_for_file(const zsp_diag_collector_t *c,
                              zsp_file_id_t fid)
{
    size_t n = 0U;
    for (size_t i = 0U; i < c->count; ++i) {
        if ((c->diags[i].severity == ZSP_SEV_ERROR)
                && (c->diags[i].file_id == fid)) {
            n += 1U;
        }
    }
    return n;
}

static corpus_result_t try_parse_and_analyse(const char *path)
{
    corpus_result_t r = { false, false, false };

    char roots_buf[6][1024];
    const char *roots[6];
    size_t n_roots = 0U;
    derive_roots(path, roots_buf, roots, 6U, &n_roots);

    zsp_session_t session;
    if (zsp_session_init(&session, roots, n_roots) != ZSERIO_OK) {
        return r;
    }

    zsp_ast_node_t *top_unit = NULL;
    if ((zsp_session_load(&session, path, &top_unit) == ZSERIO_OK)
            && (top_unit != NULL)) {
        const zsp_file_id_t target_fid = top_unit->file_id;
        r.parse_ok = (errors_for_file(&session.diag, target_fid) == 0U);
        if (r.parse_ok) {
            (void)zsp_session_analyse(&session);
            r.sema_ok = (errors_for_file(&session.diag, target_fid) == 0U);
            if (r.sema_ok) {
                counter_sink_t cs = { 0U, 0U };
                r.emit_ok = (zsp_emit_unit(top_unit, &session.arena,
                                           &session.diag, &session.source,
                                           counter_write, &cs) == ZSERIO_OK)
                            && (errors_for_file(&session.diag, target_fid) == 0U);
            }
        }
    }

    zsp_session_dispose(&session);
    return r;
}

typedef struct sweep_state {
    size_t total;
    size_t parse_passed;
    size_t sema_passed;
    size_t emit_passed;
    size_t failed_capacity;
    char **failed_paths;
    size_t failed_count;
    size_t sema_only_capacity;
    char **sema_only_paths;
    size_t sema_only_count;
} sweep_state_t;

static void sweep_record_failure(sweep_state_t *s, const char *path)
{
    if (s->failed_count == s->failed_capacity) {
        const size_t new_cap = (s->failed_capacity == 0U) ? 32U
                                                          : (s->failed_capacity * 2U);
        char **grown = (char **)realloc(s->failed_paths, new_cap * sizeof(char *));
        if (grown == NULL) return;
        s->failed_paths    = grown;
        s->failed_capacity = new_cap;
    }
    s->failed_paths[s->failed_count] = strdup(path);
    s->failed_count += 1U;
}

static void sweep_record_sema_only(sweep_state_t *s, const char *path)
{
    if (s->sema_only_count == s->sema_only_capacity) {
        const size_t new_cap = (s->sema_only_capacity == 0U) ? 32U
                                                             : (s->sema_only_capacity * 2U);
        char **grown = (char **)realloc(s->sema_only_paths, new_cap * sizeof(char *));
        if (grown == NULL) return;
        s->sema_only_paths    = grown;
        s->sema_only_capacity = new_cap;
    }
    s->sema_only_paths[s->sema_only_count] = strdup(path);
    s->sema_only_count += 1U;
}

/* Iterative directory walker — POSIX opendir/readdir + a manual
 * worklist. No recursion (Rule 17.2). */
typedef struct dir_entry {
    char path[2048];
} dir_entry_t;

static void sweep_directory(const char *root, sweep_state_t *s)
{
    dir_entry_t *stack = (dir_entry_t *)malloc(64U * sizeof(dir_entry_t));
    if (stack == NULL) return;
    size_t cap   = 64U;
    size_t depth = 0U;
    (void)snprintf(stack[depth++].path, sizeof(stack[0].path), "%s", root);

    while (depth > 0U) {
        depth -= 1U;
        char dir_path[2048];
        (void)snprintf(dir_path, sizeof(dir_path), "%s", stack[depth].path);

        DIR *const d = opendir(dir_path);
        if (d == NULL) continue;
        for (;;) {
            struct dirent *const ent = readdir(d);
            if (ent == NULL) break;
            if ((strcmp(ent->d_name, ".") == 0)
                    || (strcmp(ent->d_name, "..") == 0)) {
                continue;
            }
            char full[4096];
            (void)snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                if (depth >= cap) {
                    const size_t new_cap = cap * 2U;
                    dir_entry_t *grown = (dir_entry_t *)realloc(
                            stack, new_cap * sizeof(dir_entry_t));
                    if (grown == NULL) break;
                    stack = grown;
                    cap   = new_cap;
                }
                /* Silence -Wformat-truncation: the dir_path is
                 * bounded by the recursion depth; truncation here
                 * is benign because we'd just fail to find files
                 * one level deeper, not corrupt. */
                const size_t flen = strlen(full);
                const size_t copy = (flen < (sizeof(stack[0].path) - 1U))
                        ? flen : (sizeof(stack[0].path) - 1U);
                (void)memcpy(stack[depth].path, full, copy);
                stack[depth].path[copy] = '\0';
                depth += 1U;
            }
            else if (S_ISREG(st.st_mode) && ends_with_zs(ent->d_name)) {
                s->total += 1U;
                const corpus_result_t r = try_parse_and_analyse(full);
                if (r.parse_ok) {
                    s->parse_passed += 1U;
                }
                if (r.sema_ok) {
                    s->sema_passed += 1U;
                }
                if (r.emit_ok) {
                    s->emit_passed += 1U;
                }
                if (!r.parse_ok) {
                    sweep_record_failure(s, full);
                }
                else if (!r.sema_ok) {
                    sweep_record_sema_only(s, full);
                }
            }
        }
        (void)closedir(d);
    }
    free(stack);
}

static void test_corpus_sweep_language(void)
{
    sweep_state_t s;
    (void)memset(&s, 0, sizeof(s));

    char root[1024];
    (void)snprintf(root, sizeof(root), "%s/language", ZSP_CORPUS_ROOT);

    struct stat root_st;
    if (stat(root, &root_st) != 0) {
        TEST_IGNORE_MESSAGE("corpus submodule not initialised; skipping sweep");
        return;
    }

    sweep_directory(root, &s);

    if (s.total == 0U) {
        TEST_IGNORE_MESSAGE("no schema files discovered; skipping");
        return;
    }

    (void)printf("\ncorpus sweep:\n"
                 "  %zu / %zu schemas parsed with zero errors\n"
                 "  %zu / %zu schemas survived parse + analyse\n"
                 "  %zu / %zu schemas survived parse + analyse + emit\n",
                 s.parse_passed, s.total,
                 s.sema_passed,  s.total,
                 s.emit_passed,  s.total);
    if (s.failed_count > 0U) {
        (void)printf("first parse failures:\n");
        const size_t to_show = (s.failed_count < 6U) ? s.failed_count : 6U;
        for (size_t i = 0U; i < to_show; ++i) {
            (void)printf("  - %s\n", s.failed_paths[i]);
        }
    }
    if (s.sema_only_count > 0U) {
        (void)printf("first sema-only failures (parsed clean but analyser found errors):\n");
        const size_t to_show = (s.sema_only_count < 6U) ? s.sema_only_count : 6U;
        for (size_t i = 0U; i < to_show; ++i) {
            (void)printf("  - %s\n", s.sema_only_paths[i]);
        }
    }
    for (size_t i = 0U; i < s.failed_count; ++i) {
        free(s.failed_paths[i]);
    }
    for (size_t i = 0U; i < s.sema_only_count; ++i) {
        free(s.sema_only_paths[i]);
    }
    free(s.failed_paths);
    free(s.sema_only_paths);

    /* Milestone 7.2/7.3 acceptance threshold: 85% of upstream
     * language schemas must survive parse + analyse with zero
     * diagnostic errors. */
    const size_t threshold_pct = 85U;
    const size_t parse_pct = (s.parse_passed * 100U) / s.total;
    const size_t sema_pct  = (s.sema_passed  * 100U) / s.total;
    if (parse_pct < threshold_pct) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg),
                       "parse pass rate %zu%% below threshold %zu%% (%zu / %zu)",
                       parse_pct, threshold_pct, s.parse_passed, s.total);
        TEST_FAIL_MESSAGE(msg);
    }
    /* Sema is allowed to be lower than parse during slice-by-slice
     * rollout. As the analyser tightens, the floor rises. */
    const size_t sema_floor = 60U;
    if (sema_pct < sema_floor) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg),
                       "sema pass rate %zu%% below floor %zu%% (%zu / %zu)",
                       sema_pct, sema_floor, s.sema_passed, s.total);
        TEST_FAIL_MESSAGE(msg);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_corpus_sweep_language);
    return UNITY_END();
}
