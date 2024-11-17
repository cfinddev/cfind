/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "test_utils.h"

static int test_pass(void);
TEST_DECL(test_pass);

/*
 * Always pass.
 */
static int
test_pass(void)
{
	return 0;
}
