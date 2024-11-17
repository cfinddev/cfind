/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "main_support.h"

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * stdio might not be set up until setup_stdio() returns.
 *
 * Write log messages to this buffer as a last resort. The contents should be
 * printable with the debugger.
 */
char cf_prelog_buf[128];
#define prelog(fmt, ...) ({ \
	snprintf(cf_prelog_buf, sizeof(cf_prelog_buf), fmt, ##__VA_ARGS__); \
})

/*
 * Make sure stdio file descriptors point to something.
 *
 * The problem this prevents is the following:
 * - the parent process is responsible for setting up stdio fds (0, 1, 2)
 * - a malicious parent process may exec cfind without stdio bound to anything
 * - the next file cfind opens may be assigned one of the stdio fds
 * - logging functions blindly write to fds 0, 1, 2 -- whatever they may be
 * - this may corrupt, say, a database file cfind opened
 * - this is a privilege escalation if an underprivileged parent gets cfind
 *   to write to some file it cannot access
 *
 * The solution is the following:
 * - fstat(2) each stdio fd
 * - if it doesn't exist, dup2(2) "/dev/null" to it
 *
 * This functions returns an errno on failure.
 */
int
cf_setup_stdio(void)
{
	struct stat sb;
	int error = 0;
	int devnull = -1;

	// check and possibly setup each stdio fd
	for (int fd = 0; fd <= 2; ++fd) {
		if (fstat(fd, &sb) == 0) {
			// already exists
			continue;
		}

		error = errno;
		if (error != EBADF) {
			// some other error
			prelog("cannot stat fd %d, error %d\n", fd, error);
			goto fail;
		}

		// maybe set up `devnull` fd for the first time
		if (devnull == -1) {
			if ((devnull = open(_PATH_DEVNULL, O_RDWR)) == -1) {
				error = errno;
				prelog("cannot open %s, error %d\n", _PATH_DEVNULL, error);
				goto fail;
			}
		}

		// alias `devnull` to `fd`
		// even if `devnull` equals `fd`, dup2(x, x) is a nop
		if (dup2(devnull, fd) == -1) {
			error = errno;
			prelog("cannot dup2(%d, %d), error %d\n", devnull, fd, error);
			goto fail;
		}
	}

	return 0;
fail:
	// Note: `devnull` is leaked because it doesn't matter
	return error;
}
