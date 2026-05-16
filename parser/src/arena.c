/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zsparser/arena.h"

#include <string.h>

/*
 * Chunked bump arena implementation. See zsparser/arena.h for the
 * design rationale. No recursion; allocations route through the
 * caller-supplied zserio_allocator_t. Chunks form a singly-linked
 * list so dispose walks them in order and frees each at its
 * original allocation size.
 */

static bool allocator_usable(const zserio_allocator_t *a)
{
    return (a != NULL) && (a->vtable != NULL)
            && (a->vtable->alloc != NULL) && (a->vtable->free != NULL);
}

zserio_status_t zsp_arena_init(zsp_arena_t *a,
                               const zserio_allocator_t *alloc)
{
    if (a == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (!allocator_usable(alloc)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    a->alloc       = alloc;
    a->first       = NULL;
    a->current     = NULL;
    a->bytes_used  = 0U;
    a->bytes_total = 0U;
    return ZSERIO_OK;
}

void zsp_arena_dispose(zsp_arena_t *a)
{
    if ((a == NULL) || (a->alloc == NULL)) {
        return;
    }
    zsp_arena_chunk_t *c = a->first;
    while (c != NULL) {
        zsp_arena_chunk_t *const next = c->next;
        a->alloc->vtable->free(a->alloc->userdata, c, c->alloc_size,
                               sizeof(void *));
        c = next;
    }
    a->first       = NULL;
    a->current     = NULL;
    a->bytes_used  = 0U;
    a->bytes_total = 0U;
    a->alloc       = NULL;
}

static zsp_arena_chunk_t *allocate_chunk(zsp_arena_t *a, size_t payload_min)
{
    /* Choose chunk payload as max(default, payload_min). The
     * chunk header lives in the same allocation. */
    size_t payload = ZSP_ARENA_CHUNK_BYTES;
    if (payload < payload_min) {
        payload = payload_min;
    }
    const size_t total = sizeof(zsp_arena_chunk_t) + payload;

    void *const block = a->alloc->vtable->alloc(a->alloc->userdata,
                                                total, sizeof(void *));
    if (block == NULL) {
        return NULL;
    }
    zsp_arena_chunk_t *const c = (zsp_arena_chunk_t *)block;
    c->next       = NULL;
    /* Payload lives immediately after the header. The cast through
     * uintptr_t avoids Rule 11.5 (void* to non-void cast); the
     * runtime allocator deviation D004 already covers this site
     * convention. */
    c->bytes      = (uint8_t *)c + sizeof(zsp_arena_chunk_t);
    c->cap        = payload;
    c->used       = 0U;
    c->alloc_size = total;

    if (a->first == NULL) {
        a->first = c;
    }
    else {
        a->current->next = c;
    }
    a->current      = c;
    a->bytes_total += payload;
    return c;
}

void *zsp_arena_alloc(zsp_arena_t *a, size_t size, size_t align)
{
    if ((a == NULL) || (size == 0U) || (align == 0U)) {
        return NULL;
    }

    /* Round `used` up to alignment. */
    const size_t mask = align - 1U;

    /* Try the current chunk first. */
    if (a->current != NULL) {
        const size_t aligned = (a->current->used + mask) & ~mask;
        if ((aligned <= a->current->cap)
                && (size <= (a->current->cap - aligned))) {
            uint8_t *const p = a->current->bytes + aligned;
            a->current->used = aligned + size;
            a->bytes_used   += size;
            (void)memset(p, 0, size);
            return p;
        }
    }

    /* Need a fresh chunk. Reserve enough payload for the request
     * plus its alignment slack in the worst case. */
    const size_t payload_min = size + align;
    zsp_arena_chunk_t *const c = allocate_chunk(a, payload_min);
    if (c == NULL) {
        return NULL;
    }
    const size_t aligned = (c->used + mask) & ~mask;
    uint8_t *const p = c->bytes + aligned;
    c->used         = aligned + size;
    a->bytes_used  += size;
    (void)memset(p, 0, size);
    return p;
}

char *zsp_arena_strdup(zsp_arena_t *a, const char *src, size_t len)
{
    if ((a == NULL) || (src == NULL)) {
        return NULL;
    }
    char *const dst = (char *)zsp_arena_alloc(a, len + 1U, 1U);
    if (dst == NULL) {
        return NULL;
    }
    if (len > 0U) {
        (void)memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return dst;
}
