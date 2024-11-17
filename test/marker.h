/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Test support file to parse line/column markers in test source snippets.
 */
#pragma once

#include "../cc_support.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

__BEGIN_DECLS

/*
 * A position in a string of source code.
 */
typedef struct {
	uint32_t line;
	uint32_t column;
} source_line_t;

/*
 * A list of parsed marker positions.
 *
 * The point of markers is to have tests symbolically refer to line/column
 * positions in C source passed to the indexer as opposed to hard coding them.
 *
 * E.g., if a test wanted to
 * - index a line of C code:
 *   "struct { ... } foo;"
 * - check a typename entry for 'foo' is created
 * - check its source location is line 1, column 16
 *
 * It would normally have to hardcode the position within in the test function.
 * With markers, the C code above can instead be
 *   "struct { ... } / *@@>0* /foo;"
 *
 * The test can then
 * - parse the C code for markers
 * - run the indexer
 * - check 'foo's line/column equals those of marker 0
 * 
 * (Note: the syntax for markers is more accurately described by the docs for
 * find_markers(). A marker is a C89 comment, but that cannot be nested in the
 * comment used for this doc.)
 */
typedef struct {
	source_line_t *markers;
	size_t n;
} source_marker_t;

// API
int find_markers(const char *source, size_t n, source_marker_t *out);

// the following are only exposed for testing "marker.c"

typedef struct {
	unsigned id;
	uint8_t num_bytes;
	bool points_right;
} marker_t;

bool parse_unsigned(const char *s, size_t n, uint8_t *n_bytes, unsigned *out);
int parse_marker(const char *source, size_t n, marker_t *out);

__END_DECLS
