/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * String tokenization support.
 */
#pragma once

#include "cc_support.h"
#include "cf_string.h"

#include <stdbool.h>

__BEGIN_DECLS

/*
 * token iterator
 *
 * Saved state for cfind's version of strtok(3).
 * Use is similar to other iterators.
 *   make()
 *   while (next()) {
 *     peek
 *   }
 *   free()
 *
 * Members
 * - str
 *   The string to scan. Unlike strtok(3), the underlying char buffer is not
 *   mutated because slices can be retured as `cf_str_t`.
 * - cur
 *   Pointer to start of current token. Before the first next() call, this has
 *   value NULL.
 * - end
 *   Exclusive end pointer to the end of the current token. In picture form:
 *      foo
 *      ^  ^
 *      |  end
 *      cur 
 */
typedef struct {
	cf_str_t str;
	const char *cur;
	const char *end;
} cf_tok_iter_t;

void tok_iter_make(const cf_str_t *str, cf_tok_iter_t *out);
void tok_iter_free(cf_tok_iter_t *iter);
bool tok_iter_next(cf_tok_iter_t *iter);
void tok_iter_peek(cf_tok_iter_t *iter, cf_str_t *out);

__END_DECLS
