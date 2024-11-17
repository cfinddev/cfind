/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Logging utilities.
 */
#pragma once

#include <stdio.h>

/*
 * Function-like macros to print different types of messages.
 *
 * The following list of macros is sorted (lowest frequency, highest severity)
 * to (greatest frequency, no severity).
 * - cf_print_corrupt
 *   Use this for database corruption.
 * - cf_print_err
 *   Use for internal errors where an assertion (crashing) isn't appropriate.
 *   E.g. if clang or sqlite behaves in an unexpected way.
 * - cf_print_debug
 *   Use for internal errors that are less harmful than `cf_print_err`.
 * - cf_print_warn
 *   Use this to warn about user inputs. E.g., strange ASTs where some parts
 *   won't be indexed.
 * - cf_print_info
 *   Highest frequency. Use this during development to help trace execution
 *   through the code. Expect this to be disabled.
 *
 * These exist as different macros so they can be individually disabled in this
 * centralized location.
 */
#define cf_print_corrupt(fmt, ...) cf_print_raw(fmt, ##__VA_ARGS__)
#define cf_print_err(fmt, ...) cf_print_raw(fmt, ##__VA_ARGS__)
#define cf_print_debug(fmt, ...) cf_print_raw(fmt, ##__VA_ARGS__)
#define cf_print_warn(fmt, ...) cf_print_raw(fmt, ##__VA_ARGS__)
#define cf_print_info(fmt, ...) cf_print_raw(fmt, ##__VA_ARGS__)

/*
 * Raw print macro.
 *
 * Don't use directly. Prefer the `cf_print_` macros instead.
 *
 * This adds the following prefix to each printout:
 *   FILE:LINE: ...
 */
#define cf_print_raw(fmt, ...) \
	printf("%s:%u: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

/*
 * Printf format adjuster.
 *
 * FreeBSD, OpenBSD, and GNU/linux each have different definitions for
 * `int64_t` on amd64. It's either a typedef of `long` or of `long long`
 * (despite the ABI saying `long long` is also a 64bit type).
 *
 * This is problematic for printf(3) because "-Wformat" will complain about
 * using a "%lld" to print an argument of type `long`.
 *
 * The "correct" thing to use `PRId64` as the format specifier. However, this
 * is too ugly because it requires breaking the printf string apart and
 * replacing a 4 char `%lld` with an 11 char `%" PRId64 "`.
 *
 * Fix this on the argument side by promoting `long` to `long long` on the
 * platforms that are problematic. Use it like
 *   int64_t id;
 *   cf_print_info("id=%lld\n", p_(id));
 */
#define p_(x) _Generic(x, \
	long: ((long long)x), \
	unsigned long: ((unsigned long long)x), \
	default: x \
)

_Static_assert(sizeof(long long) == 8,
		"Make sure `int64_t` can be converted to `long long`.");
