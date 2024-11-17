/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Macros for declaring a test case, and for accessing the list of all tests.
 */
#pragma once

#include "../cc_support.h"

__BEGIN_DECLS

/*
 * Object file section in which all test case entries are placed.
 */
#define TEST_SECTION_NAME "cf_tests"

/*
 * Attribute that places a `test_case_t` variable into the test section.
 */
#define TEST_SECTION_ADD __attribute__((used, __section__(TEST_SECTION_NAME)))

#define STRINGIFY_(X) #X
#define STRINGIFY(X) STRINGIFY_(X)

/*
 * A test case description.
 */
typedef struct {
	int (*test)(void);
	const char *name;
} test_case_t;

#define TEST_DECL(test_) \
	static const TEST_SECTION_ADD test_case_t _test_entry_ ## test_ = { \
		.test = (test_), \
		.name = STRINGIFY(test_), \
	}

/*
 * XXX NOTE: this syntax is specific to GNU ld. It probably needs to be changed
 * if tests are to be built on a different platform with a different linker.
 */
#define TEST_SECTION_START __asm__("__start_" TEST_SECTION_NAME)
#define TEST_SECTION_STOP __asm__("__stop_" TEST_SECTION_NAME)

__END_DECLS
