/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Main entry point to running tests.
 *
 * During build the linker collects all test cases into an array in section
 * TEST_SECTION_START. This file executes each test case (in the same process)
 * and prints nice messages to stdio.
 */
#include "../main_support.h"
#include "test_runner.h"

#include <stdint.h>
#include <stdio.h>
#include <sysexits.h>

extern const test_case_t tests_start[] TEST_SECTION_START;
extern const test_case_t tests_end[] TEST_SECTION_STOP;

static int
run_one_test(const test_case_t *test)
{
	printf("[START] %s\n", test->name);
	const int ret = test->test();
	if (ret) {
		printf("[FAIL] %s\n", test->name);
	} else {
		printf("[PASS] %s\n", test->name);
	}

	return ret;
}

static int
run_tests(void)
{
	size_t num_fail = 0;
	size_t num_pass = 0;

	const size_t num_tests = (size_t)(tests_end - tests_start);
	printf("running %zu tests\n", num_tests);
	for (size_t i = 0; i < num_tests; ++i) {
		int ret = run_one_test(&tests_start[i]);
		if (ret) {
			++num_fail;
		} else {
			++num_pass;
		}
	}

	printf("%zu / %zu pass; %zu fail\n", num_pass, num_tests, num_fail);
	return (int)num_fail;
}

int
main(void)
{
	int error;
	if ((error = cf_setup_stdio())) {
		return error;
	}
	if (run_tests()) {
		return EX_SOFTWARE;
	}
	return 0;
}
