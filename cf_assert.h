/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#pragma once

#include <stdio.h>

/*
 * Assertion similar to assert(3).
 *
 * It's different in that:
 * - it's a statement rather than an expression
 * - its on/off status is configured in the source rather than by the weird
 *   NDEBUG macro
 *
 * Use it to express assumptions in code that are supposed to be guaranteed by
 * other parts of cfind *only*. Don't use it to make assumptions about the
 * behavior of external software: clang, sqlite, kernel, old cfind versions.
 *
 * If an assertion fails, the behavior is unspecified. The point is more to
 * express assumptions rather than do anything specific.
 */
#define cf_assert(expr) do { \
	if (!!!(expr)) { \
		cf_assert_fail("%s\n", #expr); \
	} \
} while (0)

/*
 * Fail an assertion with an error message.
 *
 * Sometimes it's too complicated to write an assertion that tests a single
 * expression. Use this macro in the same circumstance that an assertion should
 * fail. E.g.:
 *   switch (bitmask) {
 *     case FLAG1:
 *     ...
 *     case FLAG2 | FLAG5:
 *       // ok values
 *       break;
 *     default:
 *       cf_assert_fail("bad flags: %x\n", bitmask);
 *   }
 *
 * This differs from cf_panic() in that assertions are intended to be
 * configurable (turned off) whereas cf_panic() is not.
 */
#define cf_assert_fail(fmt, ...) do { \
	printf("assertion failed: %s:%u: " fmt, \
			__FILE__, __LINE__, ##__VA_ARGS__); \
	__builtin_trap(); \
} while (0)

/*
 * Crash with an error message.
 */
#define cf_panic(fmt, ...) do { \
	printf("cf panic: %s:%u: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
	__builtin_trap(); \
} while (0)
