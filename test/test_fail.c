/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "test_utils.h"

static int test_fail(void);
TEST_DECL(test_fail);

/*
 * Always fail.
 */
static int
test_fail(void)
{
	return 1;
}
