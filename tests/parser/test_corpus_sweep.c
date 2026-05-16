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
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/source.h"

#ifndef ZSP_CORPUS_ROOT
#define ZSP_CORPUS_ROOT ""
#endif

static zserio_allocator_t libc_alloc;

void setUp(void) { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE *const f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0L, SEEK_END) != 0) { (void)fclose(f); return NULL; }
    const long sz = ftell(f);
    if ((sz < 0) || (fseek(f, 0L, SEEK_SET) != 0)) {
        (void)fclose(f);
        return NULL;
    }
    uint8_t *const buf = (uint8_t *)malloc((size_t)sz + 1U);
    if (buf == NULL) { (void)fclose(f); return NULL; }
    const size_t got = fread(buf, 1U, (size_t)sz, f);
    (void)fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = (uint8_t)'\0';
    *out_len = (size_t)sz;
    return buf;
}

static bool ends_with_zs(const char *name)
{
    const size_t n = strlen(name);
    return (n >= 3U) && (strcmp(&name[n - 3U], ".zs") == 0);
}

typedef struct corpus_result {
    bool parse_ok;
    bool sema_ok;
} corpus_result_t;

static corpus_result_t try_parse_and_analyse(const char *path)
{
    corpus_result_t r = { false, false };
    size_t len = 0U;
    uint8_t *const bytes = read_file(path, &len);
    if (bytes == NULL) return r;

    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        fid;
    zsp_ast_node_t      *unit = NULL;
    zsp_sema_t           sema;

    if ((zsp_source_init(&src, &libc_alloc) == ZSERIO_OK)
            && (zsp_arena_init(&arena, &libc_alloc) == ZSERIO_OK)
            && (zsp_diag_init(&diag, &libc_alloc, 64U) == ZSERIO_OK)
            && (zsp_source_add_borrowed(&src, path, strlen(path),
                                        bytes, len, &fid) == ZSERIO_OK)
            && (zsp_parse_unit(&src, fid, &arena, &diag, &unit) == ZSERIO_OK)
            && (unit != NULL)) {
        const size_t parse_errs = zsp_diag_error_count(&diag);
        r.parse_ok = (parse_errs == 0U);
        if (r.parse_ok) {
            if ((zsp_sema_init(&sema, &src, &arena, &diag) == ZSERIO_OK)
                    && (zsp_sema_analyse_unit(&sema, unit) == ZSERIO_OK)) {
                r.sema_ok = (zsp_diag_error_count(&diag) == parse_errs);
            }
        }
    }

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
    free(bytes);
    return r;
}

typedef struct sweep_state {
    size_t total;
    size_t parse_passed;
    size_t sema_passed;
    size_t failed_capacity;
    char **failed_paths;
    size_t failed_count;
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
                if (!r.parse_ok) {
                    sweep_record_failure(s, full);
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
                 "  %zu / %zu schemas survived parse + analyse\n",
                 s.parse_passed, s.total,
                 s.sema_passed, s.total);
    if (s.failed_count > 0U) {
        (void)printf("first parse failures:\n");
        const size_t to_show = (s.failed_count < 8U) ? s.failed_count : 8U;
        for (size_t i = 0U; i < to_show; ++i) {
            (void)printf("  - %s\n", s.failed_paths[i]);
        }
    }
    for (size_t i = 0U; i < s.failed_count; ++i) {
        free(s.failed_paths[i]);
    }
    free(s.failed_paths);

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
