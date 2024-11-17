/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

/*
 * A test not a part of cf test infrastructure.
 *
 * Test CLI tools' use of cf_setup_stdio(). Close stdio file descriptors, then
 * exec the command specified by the first argument.
 * Example use:
 *   ./test_exec ./cfind-index -s t.c
 *
 * The expected behavior is that the command works as intended except for
 * printing to stdout/stderr.
 */
int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: test_exec command [ARGS]...\n");
		return EX_USAGE;
	}
	const char *prog = argv[1];
	if (access(prog, R_OK | X_OK) == -1) {
		fprintf(stderr, "cant test exec of '%s', error %d\n", prog, errno);
		return EX_NOINPUT;
	}

	// set up bad environment
	// note: no printing after this point
	for (int i = 0; i <= 2; ++i) {
		if (close(i) == -1) {
			return errno;
		}
	}

	if (execv(prog, &argv[1]) == -1) {
		return errno;
	}
	__builtin_unreachable();
}
