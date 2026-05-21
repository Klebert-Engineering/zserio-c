/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include "zsparser/source.h"

#include <string.h>

/*
 * Source manager implementation. See zsparser/source.h for the
 * design rationale. All allocation flows through the caller-
 * supplied zserio_allocator_t. Line-start tables are built lazily
 * on first line/col lookup per file.
 */

static bool allocator_usable(const zserio_allocator_t *a)
{
    if (a == NULL) {
        return false;
    }
    if (a->vtable == NULL) {
        return false;
    }
    if (a->vtable->alloc == NULL) {
        return false;
    }
    if (a->vtable->free == NULL) {
        return false;
    }
    return true;
}

static void *call_alloc(const zserio_allocator_t *a, size_t size, size_t align)
{
    return a->vtable->alloc(a->userdata, size, align);
}

static void call_free(const zserio_allocator_t *a, void *p, size_t size, size_t align)
{
    a->vtable->free(a->userdata, p, size, align);
}

static void *call_realloc(const zserio_allocator_t *a,
                          void *p, size_t old_size, size_t new_size, size_t align)
{
    /* Match the allocator vtable contract from
     * runtime/include/zserio/allocator.h: realloc MAY be NULL; in
     * that case fall back to alloc + memcpy + free. The realloc
     * fallback never touches partial state on failure. */
    if (a->vtable->realloc != NULL) {
        return a->vtable->realloc(a->userdata, p, old_size, new_size, align);
    }

    void *const grown = call_alloc(a, new_size, align);
    if (grown == NULL) {
        return NULL;
    }
    const size_t to_copy = (old_size < new_size) ? old_size : new_size;
    if ((p != NULL) && (to_copy > 0U)) {
        (void)memcpy(grown, p, to_copy);
    }
    if (p != NULL) {
        call_free(a, p, old_size, align);
    }
    return grown;
}

zserio_status_t zsp_source_init(zsp_source_t *src,
                                const zserio_allocator_t *alloc)
{
    if (src == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (!allocator_usable(alloc)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    src->alloc = alloc;
    src->files = NULL;
    src->count = 0U;
    src->cap   = 0U;

    const size_t initial_bytes = ZSP_SOURCE_INITIAL_CAP * sizeof(zsp_file_t);
    zsp_file_t *const buf = (zsp_file_t *)call_alloc(alloc, initial_bytes,
                                                     sizeof(zsp_file_t));
    if (buf == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    (void)memset(buf, 0, initial_bytes);
    src->files = buf;
    src->cap   = ZSP_SOURCE_INITIAL_CAP;
    return ZSERIO_OK;
}

void zsp_source_dispose(zsp_source_t *src)
{
    if (src == NULL) {
        return;
    }
    if (src->alloc == NULL) {
        /* never initialised */
        return;
    }

    for (size_t i = 0U; i < src->count; ++i) {
        zsp_file_t *const f = &src->files[i];
        if (f->name_owned != NULL) {
            call_free(src->alloc, f->name_owned, f->name_alloc_size, 1U);
        }
        if (f->bytes_owned != NULL) {
            call_free(src->alloc, f->bytes_owned, f->bytes_alloc_size, 1U);
        }
        if (f->line_starts != NULL) {
            call_free(src->alloc, f->line_starts,
                      f->line_cap * sizeof(uint32_t),
                      sizeof(uint32_t));
        }
    }

    if (src->files != NULL) {
        call_free(src->alloc, src->files,
                  src->cap * sizeof(zsp_file_t),
                  sizeof(zsp_file_t));
    }

    src->files = NULL;
    src->count = 0U;
    src->cap   = 0U;
    src->alloc = NULL;
}

static zserio_status_t grow_files(zsp_source_t *src)
{
    if (src->count < src->cap) {
        return ZSERIO_OK;
    }
    if (src->cap >= ZSP_SOURCE_MAX_FILES) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }

    size_t new_cap = src->cap * 2U;
    if (new_cap > ZSP_SOURCE_MAX_FILES) {
        new_cap = ZSP_SOURCE_MAX_FILES;
    }

    const size_t old_bytes = src->cap * sizeof(zsp_file_t);
    const size_t new_bytes = new_cap * sizeof(zsp_file_t);
    zsp_file_t *const grown = (zsp_file_t *)call_realloc(
            src->alloc, src->files, old_bytes, new_bytes,
            sizeof(zsp_file_t));
    if (grown == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    (void)memset(&grown[src->cap], 0, new_bytes - old_bytes);
    src->files = grown;
    src->cap   = new_cap;
    return ZSERIO_OK;
}

static void init_file_entry(zsp_file_t *f,
                            const char *name, size_t name_len,
                            char *name_owned, size_t name_alloc_size,
                            const uint8_t *bytes, size_t bytes_len,
                            uint8_t *bytes_owned, size_t bytes_alloc_size)
{
    f->name             = name;
    f->name_len         = name_len;
    f->bytes            = bytes;
    f->len              = bytes_len;
    f->name_owned       = name_owned;
    f->name_alloc_size  = name_alloc_size;
    f->bytes_owned      = bytes_owned;
    f->bytes_alloc_size = bytes_alloc_size;
    f->line_starts      = NULL;
    f->line_count       = 0U;
    f->line_cap         = 0U;
}

zserio_status_t zsp_source_add_borrowed(
        zsp_source_t *src,
        const char *name, size_t name_len,
        const uint8_t *bytes, size_t bytes_len,
        zsp_file_id_t *out_id)
{
    if (src == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((name == NULL) || (bytes == NULL) || (out_id == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (name_len == 0U) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (src->count >= ZSP_SOURCE_MAX_FILES) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }

    ZSERIO_TRY(grow_files(src));

    init_file_entry(&src->files[src->count],
                    name, name_len, NULL, 0U,
                    bytes, bytes_len, NULL, 0U);
    *out_id = (zsp_file_id_t)src->count;
    src->count += 1U;
    return ZSERIO_OK;
}

zserio_status_t zsp_source_add_owned(
        zsp_source_t *src,
        char *name, size_t name_len, size_t name_alloc_size,
        uint8_t *bytes, size_t bytes_len, size_t bytes_alloc_size,
        zsp_file_id_t *out_id)
{
    if (src == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((name == NULL) || (bytes == NULL) || (out_id == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (name_len == 0U) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (name_alloc_size < (name_len + 1U)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (bytes_alloc_size < bytes_len) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (src->count >= ZSP_SOURCE_MAX_FILES) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }

    ZSERIO_TRY(grow_files(src));

    init_file_entry(&src->files[src->count],
                    name,  name_len,  name,  name_alloc_size,
                    bytes, bytes_len, bytes, bytes_alloc_size);
    *out_id = (zsp_file_id_t)src->count;
    src->count += 1U;
    return ZSERIO_OK;
}

bool zsp_source_get(const zsp_source_t *src, zsp_file_id_t id,
                    const zsp_file_t **out)
{
    if ((src == NULL) || (out == NULL)) {
        return false;
    }
    if (id == ZSP_FILE_ID_INVALID) {
        return false;
    }
    const size_t idx = (size_t)id;
    if (idx >= src->count) {
        return false;
    }
    *out = &src->files[idx];
    return true;
}

/*
 * Build the line-start table for a file in a single forward pass
 * over its bytes. We treat '\n' as the line terminator; '\r\n' is
 * absorbed because the '\n' starts the next line and the '\r'
 * remains as the last byte of the prior line (which carat-pointed
 * snippets render harmlessly).
 *
 * Capacity grows by doubling — same approach as Q8's array
 * growth, applied here so the table costs O(N) total work even
 * for files with millions of lines.
 */
static zserio_status_t append_line_start(zsp_file_t *f,
                                         const zserio_allocator_t *alloc,
                                         uint32_t offset)
{
    if (f->line_count == f->line_cap) {
        const size_t new_cap = (f->line_cap == 0U) ? 64U : (f->line_cap * 2U);
        const size_t old_bytes = f->line_cap * sizeof(uint32_t);
        const size_t new_bytes = new_cap * sizeof(uint32_t);
        uint32_t *const grown = (uint32_t *)call_realloc(
                alloc, f->line_starts, old_bytes, new_bytes,
                sizeof(uint32_t));
        if (grown == NULL) {
            return ZSERIO_ERR_OUT_OF_MEMORY;
        }
        f->line_starts = grown;
        f->line_cap    = new_cap;
    }
    f->line_starts[f->line_count] = offset;
    f->line_count += 1U;
    return ZSERIO_OK;
}

static zserio_status_t build_line_starts(zsp_file_t *f,
                                         const zserio_allocator_t *alloc)
{
    if (f->line_starts != NULL) {
        return ZSERIO_OK;
    }
    ZSERIO_TRY(append_line_start(f, alloc, 0U));
    for (size_t i = 0U; i < f->len; ++i) {
        if (f->bytes[i] == (uint8_t)'\n') {
            const uint32_t next = (uint32_t)(i + 1U);
            ZSERIO_TRY(append_line_start(f, alloc, next));
        }
    }
    return ZSERIO_OK;
}

/* Binary search for the largest line index i such that
 * line_starts[i] <= offset. Precondition: line_starts[0] == 0. */
static size_t find_line_index(const zsp_file_t *f, uint32_t offset)
{
    size_t lo = 0U;
    size_t hi = f->line_count;
    while ((hi - lo) > 1U) {
        const size_t mid = lo + ((hi - lo) / 2U);
        if (f->line_starts[mid] <= offset) {
            lo = mid;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

zserio_status_t zsp_source_line_col(
        zsp_source_t *src, zsp_file_id_t id, uint32_t offset,
        uint32_t *out_line, uint32_t *out_col)
{
    if (src == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (id == ZSP_FILE_ID_INVALID) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    const size_t idx = (size_t)id;
    if (idx >= src->count) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    zsp_file_t *const f = &src->files[idx];
    ZSERIO_TRY(build_line_starts(f, src->alloc));

    /* Clamp offset to file length so EOF positions resolve to the
     * end of the last line, not past it. */
    uint32_t off = offset;
    if ((size_t)off > f->len) {
        off = (uint32_t)f->len;
    }

    const size_t line_index = find_line_index(f, off);
    if (out_line != NULL) {
        *out_line = (uint32_t)(line_index + 1U);
    }
    if (out_col != NULL) {
        *out_col = (off - f->line_starts[line_index]) + 1U;
    }
    return ZSERIO_OK;
}

zserio_status_t zsp_source_line_text(
        zsp_source_t *src, zsp_file_id_t id, uint32_t line,
        const uint8_t **out_bytes, size_t *out_len)
{
    if ((src == NULL) || (out_bytes == NULL) || (out_len == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (line == 0U) {
        return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
    }
    if (id == ZSP_FILE_ID_INVALID) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    const size_t idx = (size_t)id;
    if (idx >= src->count) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    zsp_file_t *const f = &src->files[idx];
    ZSERIO_TRY(build_line_starts(f, src->alloc));

    if ((size_t)line > f->line_count) {
        return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
    }

    const uint32_t start = f->line_starts[line - 1U];
    uint32_t end;
    if ((size_t)line == f->line_count) {
        end = (uint32_t)f->len;
    }
    else {
        end = f->line_starts[line];
        /* Strip the trailing '\n' (and an optional preceding '\r')
         * so the rendered snippet is the visible line content. */
        if ((end > start) && (f->bytes[end - 1U] == (uint8_t)'\n')) {
            end -= 1U;
        }
        if ((end > start) && (f->bytes[end - 1U] == (uint8_t)'\r')) {
            end -= 1U;
        }
    }

    *out_bytes = &f->bytes[start];
    *out_len   = (size_t)(end - start);
    return ZSERIO_OK;
}
