/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "marker.h"

#include "../cf_assert.h"
#include "../cf_print.h"
#include "../cf_vector.h"

#include <limits.h>
#include <string.h>
#include <stdint.h>
#include <sys/param.h>

CF_VEC_GENERATE(marker_vec_t, source_line_t, mvec);

int parse_marker(const char *source, size_t n, marker_t *out);
bool parse_unsigned(const char *s, size_t n, uint8_t *n_bytes, unsigned *out);
static void line_from_marker(const marker_t *marker, unsigned line,
		unsigned column, source_line_t *out);

/*
 * Manually parse `source` for all markers, storing them in `out`.
 *
 * Markers can be described by pcre regex form `[/]*@@[<>][0-9]+\*[/]`. The
 * unique IDs of each marker in `source` must be sequentially ordered
 * 0, 1, 2, ... The array index of `out->markers` is used for the marker ID.
 *
 * Each marker in `out` contains its line/column location. This assumes that
 * `source[0]` has location line/column (1, 1).
 *
 * Return non-zero on failure.
 */
int
find_markers(const char *source, size_t n, source_marker_t *out)
{
	int error;

	marker_vec_t markers;
	mvec_make(&markers);

	uint32_t line = 1;
	uint32_t column = 1;
	unsigned cur_marker_id = 0;

	for (size_t i = 0; i < n; ++i) {
		const char cur = source[i];

		// no fake newlines or internal NULs allowed
		if ((cur == '\r') || (cur == '\0')) {
			error = -1;
			goto fail;
		}

		if (cur == '\n') {
			++line;
			column = 1;
			continue;
		}

		// might be the start of a marker '/*@@'
		if (cur == '/') {
			marker_t next_marker;

			int ret = parse_marker(&source[i], n - i, &next_marker);
			if (ret == -1) {
				// malformed marker
				error = -1;
				goto fail;
			}
			if (ret) {
				// a real marker
				if (cur_marker_id != next_marker.id) {
					cf_print_debug("bad marker IDs: expected '%u', got '%u'\n",
							cur_marker_id, next_marker.id);
					error = -1;
					goto fail;
				}

				source_line_t *new_marker = mvec_reserve(&markers);
				line_from_marker(&next_marker, line, column, new_marker);
				mvec_commit(&markers, new_marker);

				column += next_marker.num_bytes;
				++cur_marker_id;

				// skip to parsing after the end of the marker
				// (minus one for the `++i`)
				i += next_marker.num_bytes - 1;
				continue;
			}
		}

		++column;
	}

	// move vector `markers` to the array in `*out`
	memset(out, 0, sizeof(*out));
	out->n = mvec_len(&markers);
	out->markers = mvec_detach(&markers);

	return 0;
fail:
	mvec_free(&markers);
	return error;
}

/*
 * Parse a single marker in `source`.
 *
 * Three return values:
 * - 0 not a marker
 * - -1 looks like a marker, but it's invalid
 * - 1 a successfully parsed marked written to `out`
 */
int
parse_marker(const char *source, size_t n, marker_t *out)
{
	uint8_t num_bytes;
#define MARKER "/*@@AN*/"
	if (n < strlen(MARKER)) {
		// too short to be a marker
		return 0;
	}

	if (strncmp(source, MARKER, 4)) {
		// wrong prefix for a marker
		return 0;
	}

	// prefix looks like a marker

	if ((source[4] != '<') && (source[4] != '>')) {
		cf_print_err("warning: not a marker `%*s'\n", 5, source);
		return -1;
	}
	out->points_right = source[4] == '>';

	if (!parse_unsigned(&source[5], n - 5, &num_bytes, &out->id)) {
		cf_print_err("warning: bad marker\n");
		return -1;
	}

	// at least two bytes should remain for "*/"
	if ((n - 5 - num_bytes) < 2) {
		cf_print_err("warning: no marker end\n");
		return -1;
	}

	if (strncmp(&source[5 + num_bytes], "*/", 2)) {
		cf_print_err("warning: bad marker end\n");
		return -1;
	}

	out->num_bytes = num_bytes + strlen(MARKER) - 1;

	return 1;
#undef MARKER
}

/*
 * An enhanced strtoul(3) with several modifications.
 *
 * Parse string `s` into an unsigned integer `*out_`.
 * In more detail:
 * - `s` is `n` bytes long
 *   It does not need to be NUL terminated.
 * - the number of bytes parsed is written out to `*n_bytes`
 *   `uint8_t` has enough precision for the 10 byte long "4294967295"
 *   (UINT_MAX).
 * - only unsigned integers are parsed
 *   Chars '+' or '-' are not accepted.
 *
 * This function returns true if the prefix of `s` is an `unsigned` integer.
 * Parsing stops early on the first non-digit.
 *
 * Note:
 * - `n_bytes` isn't set on failure
 * - only decimal base is supported
 *   otherwise the current implementation could overflow `n_bytes` if it parses
 *   more than UINT8_MAX bytes of '0' chars
 */
bool
parse_unsigned(const char *s, size_t n, uint8_t *n_bytes, unsigned *out_)
{
	if (!n) {
		// an empty string isn't an integer
		return false;
	}

	// check '0[0-9]'
	if ((n >= 2) && (s[0] == '0') && ('0' <= s[1]) && (s[1] <= '9')) {
		// only decimal is supported (no octal)
		return false;
	}

	unsigned out = 0;
	bool once = false;
	size_t i;
	for (i = 0; i < n; ++i) {
		const char cur = s[i];
		if ((cur < '0') || ('9' < cur)) {
			// not an integer
			break;
		}
		// out = (out * 10) + (cur - '0');
		if (__builtin_mul_overflow(out, 10, &out) ||
				__builtin_add_overflow(out, (cur - '0'), &out)) {
			// too big
			return false;
		}
		once = true;
	}

	// return true if at least one digit was parsed
	*out_ = out;
	cf_assert(i < UINT8_MAX);
	*n_bytes = (uint8_t)i;
	return once;
}

/*
 * Given the starting location of a marker (the first "/"), determine where it
 * points to.
 *
 * Left arrow:
 *             / *@@<1* /
 *            ^ here
 * Right arrow:
 *   / *@@>1* /
 *             ^ here
 *
 * (Note: clang and vi get mad about nesting even a slash star within a C89
 * comment. Ignore the extra spaces in the above examples.)
 */
static void
line_from_marker(const marker_t *marker, unsigned line, unsigned column,
		source_line_t *out)
{
	cf_assert(line > 0);
	cf_assert(column > 0);

	out->line = line;
	if (marker->points_right) {
		out->column = column + marker->num_bytes;
	} else {
		out->column = MAX(column - 1, 1u);
	}
}
