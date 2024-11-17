/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "cf_alloc.h"
#include "cf_print.h"

#include <stdlib.h>

/*
 * Set this to `1` to enable logging of memory allocation calls.
 */
#define CF_ALLOC_DEBUG 0

#if CF_ALLOC_DEBUG
#define cf_print_alloc(fmt, ...) cf_print_debug(fmt, ##__VA_ARGS__)
#else
#define cf_print_alloc(fmt, ...)
#endif // CF_ALLOC_DEBUG

/*
 * Heap allocate `size` bytes.
 *
 * See malloc(3).
 *
 * The returned pointer is "aligned enough". It has alignment at least that of
 * `alignof(long)`. To free the allocation, call cf_free().
 *
 * On failure, return NULL. Unlike malloc(3), `errno` isn't used (and it might
 * be changed if CF_ALLOC_DEBUG is true).
 */
void *
cf_malloc(size_t size)
{
	void *const ptr = malloc(size);
	cf_print_alloc("MALLOC(%zu)->%p\n", size, ptr);
	return ptr;
}

/*
 * Free memory returned from a previous cf_malloc().
 *
 * See free(3).
 */
void
cf_free(void *ptr)
{
	cf_print_alloc("FREE(%p)\n", ptr);
	free(ptr);
}

/*
 * Change the size of a heap allocation returned from a previous cf_malloc().
 *
 * See realloc(3).
 *
 * Similar to realloc(3), don't write this
 *   void *foo = cf_malloc(...);
 *   foo = cf_realloc(foo, ...);
 * The bug is that `foo` is leaked if cf_realloc() fails.
 */
void *
cf_realloc(void *ptr, size_t size)
{
	void *const new_ptr = realloc(ptr, size);
	cf_print_alloc("REALLOC(%p, %zu)->%p\n", ptr, size, new_ptr);
	return new_ptr;
}
