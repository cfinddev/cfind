/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Compiler-specific #defines.
 *
 * It would be absurd to attempt to compile cfind with a compiler other than
 * clang, but this file lets you do that.
 */
#pragma once

#include <sys/cdefs.h> // __BEGIN_DECLS

#if __has_c_attribute(fallthrough)
#define CF_FALLTHROUGH [[fallthrough]]
#else
#define CF_FALLTHROUGH
#endif // fallthrough

#if 1 /* XXX can't find right check */
#define CF_NULLABLE _Nullable
#else
#define CF_NULLABLE
#endif // _Nullable

#if 1
#define CF_UNUSED __attribute__((unused))
#else
#define CF_UNUSED
#endif // unused

#define ARRAY_LEN(x) (sizeof(x)/sizeof(x[0]))
