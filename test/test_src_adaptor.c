/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#define _POSIX_C_SOURCE 200809L // for strnlen(3)

#include "src_adaptor.h"
#include "test_utils.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int test_src_adaptor(void);
TEST_DECL(test_src_adaptor);

/*
 * Basic test of `src_adaptor_t`.
 *
 * Test:
 * - make a source adaptor from a string
 * - open and read its file
 * - check file contents match string
 */
static int
test_src_adaptor(void)
{
	int error = 0;
#define TEST_SRC "int main(void);"
	char buf[sizeof(TEST_SRC) + 16];
	src_adaptor_t adp;

	// create a source adaptor out of TEST_SRC 
	ASSERT_EQ(make_src_adaptor(TEST_SRC, sizeof(TEST_SRC), &adp), 0);
	ASSERT_NEQ(adp.fd, -1);
	ASSERT_NEQ(strnlen(adp.path, sizeof(adp.path)), 0);

	// reopen
	int fd2 = open(adp.path, O_RDONLY);
	if (fd2 == -1) {
		error = errno;
		printf("can't reopen %s, error %d\n", adp.path, error);
		goto fail;
	}

	// reread
	ssize_t n = read(fd2, buf, sizeof(buf));
	if (n != sizeof(TEST_SRC)) {
		error = errno;
		printf("short/bad read %zd bytes, error? %d\n", n, error);
		goto fail;
	}

	// compare
	if (memcmp(TEST_SRC, buf, sizeof(TEST_SRC))) {
		error = 1;
		printf("miscompare\n");
		goto fail;
	}

fail:
	free_src_adaptor(&adp);
	return error;
}
