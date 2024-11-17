/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "cf_string.h"
#include "cf_assert.h"
#include "cf_alloc.h"

#include <string.h>
#include <errno.h>

/*
 * Set `*out` to a NULL cf string.
 *
 * This is useful for optional string data. Call cf_str_is_null() to test for a
 * such a string.
 *
 * Although not required, it's a good idea to follow with a call to
 * cf_str_free(). The behavior will be a nop.
 */
void
cf_str_null(cf_str_t *out)
{
	memset(out, 0, sizeof(*out));
	cf_assert(cf_str_is_null(out));
}

/*
 * Convenience function to make `out` borrow from `str`.
 *
 * See cf_str_borrow() for a generalized version.
 */
void
cf_str_borrow_str(const cf_str_t *src, cf_str_t *out)
{
	cf_str_borrow(src->str, cf_str_len(src), out);
}

/*
 * Set `out` to point to the same data as `str`.
 *
 * This is a borrow. The lifetime of `str` must exceed that of `out`.
 *
 * This function is useful for string literals. E.g., call
 *   cf_str_t str;
 *   cf_str_borrow("foo", 3, &str);
 *
 * Although not required, it's a good idea to follow with a call to
 * cf_str_free(). The behavior will be a nop.
 */
void
cf_str_borrow(const char *str, size_t len, cf_str_t *out)
{
	cf_assert(len <= CF_STR_MAX_LEN);

	*out = (cf_str_t) {
		.str = str,
		.len =  CF_STR_BORROWED | (uint32_t)len,
	};
}

/*
 * Promote a borrowed string to an owned string.
 *
 * Use this to extend the lifetime of a borrowed string in place.
 * Do nothing if `str` is already owned.
 *
 * On success, `str` is an owned string that contains the same contents it
 * originally did. Call cf_str_free() or else leak a heap allocation.
 *
 * On failure, `str` remains unaffected.
 */
int
cf_str_promote(cf_str_t *str)
{
	// do nothing for already-owned strings
	if (!(str->len & CF_STR_BORROWED)) {
		return 0;
	}

	return cf_str_dup(str->str, cf_str_len(str), str);
}

/*
 * Convenience function to make `out` a duplicate of `src`.
 *
 * See cf_str_dup() for a generalized version.
 */
int
cf_str_dup_str(const cf_str_t *restrict src, cf_str_t *restrict out)
{
	return cf_str_dup(src->str, cf_str_len(src), out);
}

/*
 * Similar to strdup(3).
 *
 * On success, `len` bytes from `str` will be copied into a new heap-allocated
 * buffer owned by `out`. Follow with a call to cf_str_free().
 *
 * This is useful to extend the lifetime of a string whose lifetime is either
 * too short or simply unknown. E.g.,
 *   cf_str_t str;
 *   // lifetime of `message` is only until the next strerror(3) call
 *   char *message = strerror(error);
 *   // lifetime of `str` is until cf_str_free()
 *   cf_str_dup(message, strlen(message), &str);
 */
int
cf_str_dup(const char *str, size_t len, cf_str_t *out)
{
	if (!len) {
		memset(out, 0, sizeof(*out));
		cf_assert(cf_str_is_null(out));
		return 0;
	}

	if (len > CF_STR_MAX_LEN) {
		return ERANGE;
	}

	char *new_data;
	if (!(new_data = cf_malloc(len))) {
		const int error = errno;
		cf_assert(error);
		return error;
	}

	memcpy(new_data, str, len);

	*out = (cf_str_t) {
		.str = new_data,
		.len = (uint32_t)len,
	};
	return 0;
}

/*
 * Dispose of a string returned from a previous succesful call to one of
 * cf_str_null(), cf_str_borrow(), or cf_str_dup().
 */
void
cf_str_free(cf_str_t *out)
{
	// do nothing for borrowed strings
	if (out->len & CF_STR_BORROWED) {
		return;
	}
	cf_free((void *)out->str);
}

/*
 * Return the length, in bytes, of the string pointed to by `str`.
 *
 * This simply unpacks `cf_str_t::len` by clearing the borrow bit.
 */
size_t
cf_str_len(const cf_str_t *str)
{
	return str->len & ~CF_STR_BORROWED;
}

/*
 * Return whether `str` is considered a NULL string.
 *
 * This is true if the string data is a NULL pointer, or a valid pointer to
 * size zero data.
 */
bool
cf_str_is_null(const cf_str_t *str)
{
	return !str->str || !cf_str_len(str);
}
