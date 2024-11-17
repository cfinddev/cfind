/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#define _GNU_SOURCE 1 // for memfd_create(2)
#include "src_adaptor.h"

#include "../cf_assert.h"

#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>

/*
 * Filesystem path base to objects created by memfd_create(2).
 *
 * linux specific
 */
#define BASE_FD_PATH "/proc/self/fd/"

/*
 * Name given to shm files.
 *
 * Only used for debugging (e.g., strace output). Uniqueness is not required.
 */
#define SHM_FILE_NAME "cfind_test_input"

/*
 * Create a `src_adaptor_t` from the `len`-byte length contents of `src`.
 *
 * The goal is to take an in-memory input and turn it into something that can
 * be open(2)ed and read(2) via the filesystem.
 *
 * With more specifics, the C source compiled in a test needs to be passed to
 * libclang. However, the libclang API only accepts filesystem paths as source
 * code input. This makes it hard to pass in a code snippet string literal
 * embedded in a test.
 *
 * This function serves the purpose of
 *   (test) -> string -> file -> (libclang) -> open -> read -> string
 *
 * Usually this sort of thing is achieved by writing to "/tmp", but that's
 * unsatisfactory because that hits disk for otherwise short lived inputs.
 * Writing to tmpfs is an improvement but it's accompanied by the challenge of
 * dealing with permissions and name conflicts.
 * memfd_create(2) fills the use case the best with the disadvantage of being
 * linux specific. In case testing is to be supported on other platforms, they
 * can just use a different implementation using posix shm_open(3).
 *
 * On success, `out->buf` contains the filesystem path to an fs object with the
 * contents of `src`. Follow with a call to free_src_adaptor().
 * On failure, this function returns an errno.
 *
 * Note: `len` excludes the NUL terminator.
 */
int
make_src_adaptor(const char *src, size_t len, src_adaptor_t *out)
{
	cf_assert(len < SSIZE_MAX);
	int error;
	int fd;

	// open shared memory file
	if ((fd = memfd_create(SHM_FILE_NAME, 0)) == -1) {
		error = errno;
		cf_assert(error);
		goto fail;
	}

	// write `src` to `fd`
	ssize_t n;
	if ((n = write(fd, src, len)) != (ssize_t)len) {
		error = errno;
		cf_assert(error);
		goto fail_write;
	}

	// init `out` fd
	out->fd = fd;

	// format filesystem path to `fd` so it can be reopened by clang
	int n2 = snprintf(out->path, sizeof(out->path), "%s%d", BASE_FD_PATH, fd);

	if (n2 >= (int)sizeof(out->path)) {
		// truncation (shouldn't happen)
		error = EMFILE;
		goto fail_write;
	}

	return 0;
fail_write:
	close(fd);
fail:
	return error;
}

/*
 * Free a source adaptor created from a previous successful call to
 * make_src_adaptor().
 */
void
free_src_adaptor(src_adaptor_t *adp)
{
	close(adp->fd);
}
