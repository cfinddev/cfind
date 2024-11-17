/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "../cf_alloc.h"
#include "marker.h"
#include "test_utils.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static int test_marker(void);
TEST_DECL(test_marker);

#define ASSERT_LIT(str) do { \
	_Static_assert(__builtin_constant_p(str), \
			"input " #str " is not a literal"); \
} while (0)

/*
 * The following are convenience adaptors to make passing in string literals
 * more concise.
 *
 * These macros accept a string literal as the first parameter and
 * automatically pass in the string length to the real function:
 * - parse_unsigned()
 * - parse_marker()
 * - find_markers()
 */

#define parse_unsignedx(str, len_out, val_out) ({ \
	ASSERT_LIT(str); \
	parse_unsigned(str, strlen(str), len_out, val_out); \
})

#define parse_markerx(str, marker_out) ({ \
	ASSERT_LIT(str); \
	parse_marker(str, strlen(str), marker_out); \
})

#define find_markersx(str, markers_out) ({ \
	ASSERT_LIT(str); \
	find_markers(str, strlen(str), markers_out); \
})

/*
 * Test parse_unsigned() works in parsing strings to `unsigned`.
 *
 * Test the following succeed:
 * - range [0, UINT_MAX]
 * - just the prefix of a string
 *
 * Negatively test:
 * - string with no integers
 * - overflow of `val`
 */
static int
test_parse_int(void)
{
	uint8_t len;
	unsigned val;

	ASSERT(parse_unsignedx("0", &len, &val));
	ASSERT_EQ(len, 1);
	ASSERT_EQ(val, 0);

	ASSERT(parse_unsignedx("11", &len, &val));
	ASSERT_EQ(len, 2);
	ASSERT_EQ(val, 11);

	ASSERT(parse_unsignedx("4294967295", &len, &val));
	ASSERT_EQ(len, 10);
	ASSERT_EQ(val, 4294967295);

	ASSERT(parse_unsignedx("4294967295x", &len, &val));
	ASSERT_EQ(len, 10);
	ASSERT_EQ(val, 4294967295);

	ASSERT(!parse_unsignedx("asdf", &len, &val));
	ASSERT(!parse_unsigned("4294967296", 0, &len, &val));
	ASSERT(!parse_unsignedx("4294967296", &len, &val));
	ASSERT(!parse_unsignedx("9999999999999", &len, &val));

	return 0;
}

/*
 * Negatively test octal isn't supported.
 *
 * A bug in the original implementation could cause a very long "octal" string
 * of '0's to overflow the `uint8_t` used to track the number of characters
 * parsed.
 *
 * Check the following fail to be parsed:
 *   00
 *   0[1-9]
 *   00...00  (256 total chars)
 *
 * But also check octal-looking strings are still interpreted as decimal:
 *   0
 *   01, 1 byte length
 *   0x (parser stops at 'x')
 *   0x1
 */
static int
test_fail_parse_octal(void)
{
	char buf[256];
	memset(buf, '0', sizeof(buf));

	uint8_t len;
	unsigned val;

	// check parsing doesn't overflow `len`
	ASSERT(!parse_unsigned(buf, sizeof(buf), &len, &val));

	// simple negative tests
	ASSERT(!parse_unsignedx("00", &len, &val));
	ASSERT(!parse_unsignedx("01", &len, &val));

	// check these are still interpreted as decimal
	ASSERT(parse_unsignedx("0", &len, &val));
	ASSERT_EQ(val, 0);
	ASSERT(parse_unsigned("01", 1, &len, &val));
	ASSERT_EQ(val, 0);
	ASSERT(parse_unsignedx("0x", &len, &val));
	ASSERT_EQ(val, 0);
	ASSERT(parse_unsignedx("0x1", &len, &val));
	ASSERT_EQ(val, 0);

	return 0;
}

/*
 * Test singular marker parsing of parse_marker().
 */
static int
test_parse_marker(void)
{
	marker_t marker;

	// basic case
	ASSERT_EQ(parse_markerx("/*@@<0*/", &marker), 1);
	ASSERT_EQ(marker.id, 0);
	ASSERT_EQ(marker.num_bytes, 8);
	ASSERT_EQ(marker.points_right, 0);

	// basic with right arrow
	ASSERT_EQ(parse_markerx("/*@@>0*/", &marker), 1);
	ASSERT_EQ(marker.id, 0);
	ASSERT_EQ(marker.num_bytes, 8);
	ASSERT_EQ(marker.points_right, 1);

	// parsing stops at '\0'
	ASSERT_EQ(parse_marker("/*@@<0*/", 9, &marker), 1);
	ASSERT_EQ(marker.id, 0);
	ASSERT_EQ(marker.num_bytes, 8);
	ASSERT_EQ(marker.points_right, 0);

	// parsing stops at string length
	ASSERT_EQ(parse_marker("/*@@>1*/xxx", 8, &marker), 1);
	ASSERT_EQ(marker.id, 1);
	ASSERT_EQ(marker.num_bytes, 8);
	ASSERT_EQ(marker.points_right, 1);

	// parsing stops at `*/`
	ASSERT_EQ(parse_markerx("/*@@<1*/asdfasdf", &marker), 1);
	ASSERT_EQ(marker.id, 1);
	ASSERT_EQ(marker.num_bytes, 8);
	ASSERT_EQ(marker.points_right, 0);

	// test length calculation
	ASSERT_EQ(parse_markerx("/*@@>1234*/x", &marker), 1);
	ASSERT_EQ(marker.id, 1234);
	ASSERT_EQ(marker.num_bytes, 11);
	ASSERT_EQ(marker.points_right, 1);

	return 0;
}

/*
 * Negatively test parse_marker().
 */
static int
test_fail_parse_marker(void)
{
	marker_t marker;

	// too short
	ASSERT_EQ(parse_markerx("/*@@<1", &marker), 0);
	ASSERT_EQ(parse_marker("/*@@>0*/", 7, &marker), 0);

	// wrong prefix
	ASSERT_EQ(parse_markerx("x/*@@>0*/", &marker), 0);
	ASSERT_EQ(parse_markerx("/*@#>0*/", &marker), 0);
	ASSERT_EQ(parse_markerx("/*@\n>0*/", &marker), 0);

	// wrong arrow
	ASSERT_EQ(parse_markerx("/*@@^0*/", &marker), -1);

	// bad integer
	ASSERT_EQ(parse_markerx("/*@@<X*/", &marker), -1);
	ASSERT_EQ(parse_markerx("/*@@<-1*/", &marker), -1);
	ASSERT_EQ(parse_markerx("/*@@<0x0*/", &marker), -1);
	ASSERT_EQ(parse_markerx("/*@@<4294967296*/", &marker), -1);
	ASSERT_EQ(parse_markerx("/*@@<99999999999*/", &marker), -1);

	// bad ending
	ASSERT_EQ(parse_markerx("/*@@<1* ", &marker), -1);
	ASSERT_EQ(parse_markerx("/*@@>11* ", &marker), -1);
	ASSERT_EQ(parse_markerx("/*@@<111* ", &marker), -1);

	// marker IDs are always decimal
	ASSERT_EQ(parse_markerx("/*@@<00*/", &marker), -1);
	ASSERT_EQ(parse_markerx("/*@@<077*/", &marker), -1);

	return 0;
}

static int
test_find_no_markers(void)
{
	source_marker_t out;

	ASSERT_EQ(find_markersx("/*@not a marker*/", &out), 0);
	ASSERT_EQ(out.markers, NULL);
	ASSERT_EQ(out.n, 0);

	return 0;
}

static int
test_find_one_marker(void)
{
	source_marker_t out;

	ASSERT_EQ(find_markersx("int /*@@>0*/foo;", &out), 0);
	ASSERT_EQ(out.n, 1);
	ASSERT_EQ(out.markers[0].line, 1);
	ASSERT_EQ(out.markers[0].column, 13);

	cf_free(out.markers);
	return 0;
}

static int
test_find_many_markers(void)
{
	source_marker_t out;

	ASSERT_EQ(find_markersx("int foo;/*@@<0*/\n/*@@>1*/int bar", &out), 0);
	ASSERT_EQ(out.n, 2);
	ASSERT_EQ(out.markers[0].line, 1);
	ASSERT_EQ(out.markers[0].column, 8);
	ASSERT_EQ(out.markers[1].line, 2);
	ASSERT_EQ(out.markers[1].column, 9);

	cf_free(out.markers);
	return 0;
}

static int
test_find_marker_start(void)
{
	source_marker_t out;
	ASSERT_EQ(find_markersx("/*@@<0*/int foo;", &out), 0);
	ASSERT_EQ(out.n, 1);
	ASSERT_EQ(out.markers[0].line, 1);
	ASSERT_EQ(out.markers[0].column, 1);
	cf_free(out.markers);

	source_marker_t out2;
	ASSERT_EQ(find_markersx(";/*@@<0*/", &out2), 0);
	ASSERT_EQ(out2.n, 1);
	ASSERT_EQ(out2.markers[0].line, 1);
	ASSERT_EQ(out2.markers[0].column, 1);
	cf_free(out2.markers);

	source_marker_t out3;
	ASSERT_EQ(find_markersx("\n;/*@@<0*/", &out3), 0);
	ASSERT_EQ(out3.n, 1);
	ASSERT_EQ(out3.markers[0].line, 2);
	ASSERT_EQ(out3.markers[0].column, 1);
	cf_free(out3.markers);

	// 8 newlines
	source_marker_t out4;
	ASSERT_EQ(find_markersx("\n\n\n\n\n\n\n\n;/*@@<0*/", &out4), 0);
	ASSERT_EQ(out4.n, 1);
	ASSERT_EQ(out4.markers[0].line, 9);
	ASSERT_EQ(out4.markers[0].column, 1);
	cf_free(out4.markers);

	return 0;
}

static int
test_find_marker_end(void)
{
	source_marker_t out;

	// marker points past end of source
	ASSERT_EQ(find_markersx("int foo;/*@@>0*/", &out), 0);
	ASSERT_EQ(out.n, 1);
	ASSERT_EQ(out.markers[0].line, 1);
	ASSERT_EQ(out.markers[0].column, 17);

	cf_free(out.markers);
	return 0;
}

static int
test_find_marker_adj(void)
{
	source_marker_t out;

	ASSERT_EQ(find_markersx("int foo;/*@@>0*/\n/*@@>1*//*@@<2*/", &out), 0);
	ASSERT_EQ(out.n, 3);

	ASSERT_EQ(out.markers[0].line, 1);
	ASSERT_EQ(out.markers[0].column, 17);

	ASSERT_EQ(out.markers[1].line, 2);
	ASSERT_EQ(out.markers[1].column, 9);

	ASSERT_EQ(out.markers[2].line, 2);
	ASSERT_EQ(out.markers[2].column, 8);

	cf_free(out.markers);
	return 0;
}

/*
 * Earlier parts of the source that *look* like markers, but aren't actually
 * markers (but aren't malformed markers) shouldn't affect the parsing of later
 * real markers.
 */
static int
test_find_marker_fakeout(void)
{
	source_marker_t out;
	ASSERT_EQ(find_markersx("/*@<0*/int /*@@>0*/foo;", &out), 0);
	ASSERT_EQ(out.n, 1);
	ASSERT_EQ(out.markers[0].line, 1);
	ASSERT_EQ(out.markers[0].column, 20);
	cf_free(out.markers);

	source_marker_t out2;
	ASSERT_EQ(find_markersx("/\nint /*@@>0*/foo;", &out2), 0);
	ASSERT_EQ(out2.n, 1);
	ASSERT_EQ(out2.markers[0].line, 2);
	ASSERT_EQ(out2.markers[0].column, 13);
	cf_free(out2.markers);

	source_marker_t out3;
	ASSERT_EQ(find_markersx("/*@\n<0*/int /*@@>0*/foo;", &out3), 0);
	ASSERT_EQ(out3.n, 1);
	ASSERT_EQ(out3.markers[0].line, 2);
	ASSERT_EQ(out3.markers[0].column, 17);
	cf_free(out3.markers);

	return 0;
}

static int
test_fail_find_markers(void)
{
	source_marker_t out;

	// no internal NULs
	ASSERT_EQ(find_markers("int \0foo;/*@@<0*/", 17, &out), -1);

	// no carriage returns
	ASSERT_EQ(find_markersx("int \rfoo;/*@@<0*/", &out), -1);

	// non-sequential IDs
	ASSERT_EQ(find_markersx("int foo;/*@@<0*/\n/*@@<2*/int bar;", &out), -1);
	ASSERT_EQ(find_markersx("int foo;/*@@<0*/\n/*@@<1*/int /*@@>0*/bar;",
			&out), -1);

	return 0;
}

static int
test_find_markers(void)
{
	ASSERT_EQ(test_find_no_markers(), 0);
	ASSERT_EQ(test_find_one_marker(), 0);
	ASSERT_EQ(test_find_many_markers(), 0);
	ASSERT_EQ(test_find_marker_start(), 0);
	ASSERT_EQ(test_find_marker_end(), 0);
	ASSERT_EQ(test_find_marker_adj(), 0);
	ASSERT_EQ(test_find_marker_fakeout(), 0);
	ASSERT_EQ(test_fail_find_markers(), 0);

	return 0;
}

/*
 * Test source marker utilities.
 */
static int
test_marker(void)
{
	ASSERT_EQ(test_parse_int(), 0);
	ASSERT_EQ(test_fail_parse_octal(), 0);
	ASSERT_EQ(test_parse_marker(), 0);
	ASSERT_EQ(test_fail_parse_marker(), 0);
	ASSERT_EQ(test_find_markers(), 0);

	return 0;
}
