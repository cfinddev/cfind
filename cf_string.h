/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * String library.
 */
#pragma once

#include "cc_support.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

__BEGIN_DECLS

/*
 * Macro constants
 *
 * - CF_STR_MAX_LEN
 *   Inclusive maximum supported string length. This has the same value as
 *   INT32_MAX.
 * - CF_STR_BORROWED
 *   Bit used to signal, in-band, in `cf_str_t::len` that a `cf_str` borrows
 *   its contents from something else and thus need not be freed.
 */
#define CF_STR_MAX_LEN 0x7fffffff
#define CF_STR_BORROWED 0x80000000

/*
 * Readonly string data structure.
 *
 * This kind of string can only come from two places:
 * - string literals
 * - other non-`cf_str_t` strings
 *
 * It's not possible to create a string out of thin air because cfind doesn't
 * ever need to do that.
 *
 * Create a new `cf_str_t` with one of:
 * - cf_str_null()
 * - cf_str_borrow()
 * - cf_str_dup()
 *
 * Free it with a call to cf_str_free().
 *
 * Members
 * - str
 *   UTF-8 encoded string data. No NUL terminator.
 *   By default, `str` points to heap-allocated string buffer. If
 *   bit CF_STR_BORROWED is set in `len`, something else owns the string. In
 *   this case, `cf_str_t` acts as a string slice.
 * - len
 *   Packed length of `str` in bytes. Because lengths > INT_MAX are unneeded, 
 *   a `uint32_t` is used. The high bit specifies whether `str` is owned or
 *   borrowed. On LP64 platforms, this member is followed by four bytes of
 *   padding.
 */
typedef struct {
	const char *str;
	uint32_t len;
} cf_str_t;

void cf_str_null(cf_str_t *out);
void cf_str_borrow_str(const cf_str_t *src, cf_str_t *out);
void cf_str_borrow(const char *str, size_t len, cf_str_t *out);
int cf_str_promote(cf_str_t *str);
int cf_str_dup_str(const cf_str_t *restrict src, cf_str_t *restrict out);
int cf_str_dup(const char *str, size_t len, cf_str_t *out);
void cf_str_free(cf_str_t *out);

size_t cf_str_len(const cf_str_t *str);
bool cf_str_is_null(const cf_str_t *str);

__END_DECLS
