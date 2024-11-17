/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * cfind CLI query parsing functions.
 */
#pragma once

#include "cc_support.h"
#include "cf_string.h"
#include "search_types.h"

__BEGIN_DECLS

int parse_command(const cf_str_t *cmd_str, search_cmd_t *out);

__END_DECLS
