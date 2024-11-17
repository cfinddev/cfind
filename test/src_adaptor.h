/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#pragma once

#include "../cc_support.h"

#include <string.h>

__BEGIN_DECLS

/*
 * Structure returned from make_src_adaptor().
 *
 * Members:
 * - fd
 *   File descriptor. This needs to be close(2)ed via free_src_adaptor() when
 *   the contents of `path`s fs object are no longer needed.
 * - path
 *   Filesystem path. This can be passed to open(2).
 *   28 bytes large because:
 *   - sizeof(src_adaptor_t) = 32
 *   - large enough for to be formatted with BASE_FD_PATH "2147483647"
 */
typedef struct {
	int fd;
	char path[28];
} src_adaptor_t;

int make_src_adaptor(const char *src, size_t len, src_adaptor_t *out);
void free_src_adaptor(src_adaptor_t *adp);

__END_DECLS
