/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include "zserio/allocator.h"

#include <stdlib.h>

/*
 * libc-backed allocator: the only translation unit in the runtime
 * permitted to call malloc / realloc / free directly. Every other
 * runtime / generated source goes through the zserio_allocator_t
 * vtable (REQ-FS-001).
 *
 * The allocator is stateless. `userdata` is unused. Two instances
 * returned from zserio_allocator_libc() are interchangeable.
 *
 * Alignment handling matches upstream pmr/NewDeleteResource: the
 * `alignment` argument is currently ignored. malloc returns memory
 * aligned to alignof(max_align_t), which satisfies all alignment
 * requirements of the wire-format primitives. Callers needing
 * stricter alignment plug their own allocator.
 */

static void *libc_alloc(void *userdata, size_t size, size_t alignment)
{
    (void)userdata;
    (void)alignment;
    /* size > 0 is a precondition of zserio_allocator_t.alloc; the
     * runtime validates at the public boundary (REQ-FS-006). */
    return malloc(size);
}

static void *libc_realloc(void *userdata, void *ptr,
                          size_t old_size, size_t new_size, size_t alignment)
{
    (void)userdata;
    (void)old_size;
    (void)alignment;
    /* On failure realloc preserves the original block, matching the
     * contract documented in allocator.h. */
    return realloc(ptr, new_size);
}

static void libc_free(void *userdata, void *ptr, size_t size, size_t alignment)
{
    (void)userdata;
    (void)size;
    (void)alignment;
    free(ptr);
}

static const zserio_allocator_vtable_t LIBC_VTABLE = {
    libc_alloc,
    libc_realloc,
    libc_free,
};

zserio_allocator_t zserio_allocator_libc(void)
{
    const zserio_allocator_t a = {
        &LIBC_VTABLE,
        NULL, /* userdata: unused */
    };
    return a;
}

/*
 * Bump-arena allocator: hands out aligned sub-ranges of a caller-
 * owned byte buffer until exhausted. `free` is a no-op; the whole
 * arena is released by {@code zserio_arena_reset}.
 *
 * Alignment math assumes the requested alignment is a power of two.
 * The runtime always passes a power-of-two alignment from the
 * generator (either `sizeof(T)` for an array-of-T or `1U` for a raw
 * byte buffer), so the arena does not re-validate per call. Out-of-
 * bounds bumps return NULL, which propagates as
 * ZSERIO_ERR_OUT_OF_MEMORY through every generated reader.
 */

static void *arena_alloc(void *userdata, size_t size, size_t alignment)
{
    zserio_arena_t *const a = (zserio_arena_t *)userdata;
    /* Round `used` up to the next multiple of `alignment`. With
     * alignment a power of two this is the classic bitmask trick. */
    const size_t mask = alignment - 1U;
    const size_t aligned = (a->used + mask) & ~mask;
    /* Two-step bounds check to defeat size_t wrap on a malicious
     * (size, alignment) pair. */
    if ((aligned > a->cap) || (size > (a->cap - aligned))) {
        return NULL;
    }
    a->used = aligned + size;
    if (a->used > a->high_water) {
        a->high_water = a->used;
    }
    return a->buf + aligned;
}

static void arena_free(void *userdata, void *ptr, size_t size, size_t alignment)
{
    (void)userdata;
    (void)ptr;
    (void)size;
    (void)alignment;
    /* No-op: arena memory is released en masse by zserio_arena_reset. */
}

static const zserio_allocator_vtable_t ARENA_VTABLE = {
    arena_alloc,
    NULL, /* realloc: runtime falls back to alloc + memcpy */
    arena_free,
};

void zserio_arena_init(zserio_arena_t *a, uint8_t *buf, size_t cap)
{
    if (a == NULL) {
        return;
    }
    a->buf = buf;
    a->cap = cap;
    a->used = 0U;
    a->high_water = 0U;
}

void zserio_arena_reset(zserio_arena_t *a)
{
    if (a == NULL) {
        return;
    }
    a->used = 0U;
    /* high_water deliberately preserved across resets so callers can
     * size their backing buffer after a few warm-up passes. */
}

zserio_allocator_t zserio_allocator_arena(zserio_arena_t *a)
{
    const zserio_allocator_t out = {
        &ARENA_VTABLE,
        a, /* userdata: the arena's mutable state */
    };
    return out;
}
