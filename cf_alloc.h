/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Wrappers around libc general purpose memory allocators.
 */
#pragma once

#include "cc_support.h"

#include <string.h>

__BEGIN_DECLS

void *cf_malloc(size_t size);
void cf_free(void *ptr);
void *cf_realloc(void *ptr, size_t size);

__END_DECLS
