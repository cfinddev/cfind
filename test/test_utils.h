/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Utilities for individual test cases.
 */
#pragma once

#include "../cc_support.h"
#include "test_runner.h"

#include <stdio.h>

__BEGIN_DECLS

/*
 * Call to fail a test.
 *
 * Tests fail by returning from their test function.
 * XXX which breaks as soon as it's used from a nested utility function
 */
#define ASSERT_FAIL_(func, line, fmt, ...) do { \
	printf("%s(),%u: " fmt "\n", (func), (line), ##__VA_ARGS__); \
	return 1; \
} while(0)

#define ASSERT_FAIL(FMT, ...) \
	ASSERT_FAIL_(__func__, __LINE__, FMT, ##__VA_ARGS__)

#define ASSERT(cond) do { \
	/* XXX static_assert `cond` is a `bool` */ \
	if (!(cond)) { \
		ASSERT_FAIL("ASSERT failed: %s", STRINGIFY(cond)); \
	} \
} while(0)

#define ASSERT_EQ(lhs_, rhs_) do { \
	/* XXX not everything can be converted to ull, but getting the format \
	 * string right is tricky */ \
	const unsigned long long lhs = (unsigned long long)(lhs_); \
	const unsigned long long rhs = (unsigned long long)(rhs_); \
	if (lhs != rhs) { \
		ASSERT_FAIL("ASSERT_EQ failed: %s (%llu) != %s (%llu)", \
				STRINGIFY(lhs_), lhs, STRINGIFY(rhs_), rhs); \
	} \
} while(0)

#define ASSERT_NEQ(lhs_, rhs_) do { \
	/* XXX not everything can be converted to ull, but getting the format \
	 * string right is tricky */ \
	const unsigned long long lhs = (unsigned long long)(lhs_); \
	const unsigned long long rhs = (unsigned long long)(rhs_); \
	if (lhs == rhs) { \
		ASSERT_FAIL("ASSERT_NEQ failed: %s (%llu) == %s (%llu)", \
				STRINGIFY(lhs_), lhs, STRINGIFY(rhs_), rhs); \
	} \
} while(0)

__END_DECLS
