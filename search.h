/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#pragma once

#include "cc_support.h"
#include "cf_string.h"

__BEGIN_DECLS

int run_one_command(const char *db_path, const cf_str_t *cmd);

__END_DECLS
