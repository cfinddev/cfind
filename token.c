/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "token.h"

#include "cf_assert.h"
#include "cf_print.h"
#include "cf_string.h"

#include <stdbool.h>
#include <string.h>

static bool is_delim(char c);

void
tok_iter_make(const cf_str_t *str, cf_tok_iter_t *out)
{
	memset(out, 0, sizeof(*out));
	if (cf_str_is_null(str)) {
		cf_print_warn("tokenizing null string\n");
	}
	cf_str_borrow_str(str, &out->str);
}

void
tok_iter_free(cf_tok_iter_t *iter)
{
	cf_str_free(&iter->str);
}

/*
 * XXX
 * examples
 * str = "a b"
 * cur = ???
 * end = 0
 *
 * peek -> UB
 * next ->
 *   cur = end
 *   scan in [cur, len(str))
 *   look for first non-delim char
 *   scan > cur for first delim; be careful about end
 *   end = <stop> - 1
 *   convert to cf_str
 * peek -> [0, 1)
 *
 * stop if end == len
 *
 * ----
 * This does not invalidate strings returned from previous calls to _peek().
 *
 */
bool
tok_iter_next(cf_tok_iter_t *iter)
{
	const char *real_end = iter->str.str + cf_str_len(&iter->str);
	// handle first call setup
	if (!iter->cur) {
		iter->end = iter->str.str;
	}

	// look for first non-delim char starting from `end`
	const char *cur = iter->end;
	for (; cur < real_end; ++cur) {
		if (!is_delim(*cur)) {
			break;
		}
	}

	// no more tokens
	if (cur == real_end) {
		return false;
	}

	cf_assert(!is_delim(*cur));

	// scan for first delim after `cur`
	const char *end;
	for (end = cur+1; end < real_end; ++end) {
		if (is_delim(*end)) {
			break;
		}
	}

	iter->cur = cur;
	iter->end = end;
	return true;
}

/*
 * XXX
 *
 * The string returned via `out` has the same lifetime as the string passed
 * into _make().
 */
void
tok_iter_peek(cf_tok_iter_t *iter, cf_str_t *out)
{
	cf_assert(iter->end > iter->cur);
	cf_str_borrow(iter->cur, (size_t)(iter->end - iter->cur), out);
}

static bool
is_delim(char c)
{
	return (c == ' ') || (c == '\t');
}
