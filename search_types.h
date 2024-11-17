/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Types used by cfind CLI to describe queries for database.
 */
#pragma once

#include "cc_support.h"
#include "cf_string.h"
#include "db_types.h"

#include <stdbool.h>
#include <stdint.h>

__BEGIN_DECLS

typedef enum {
	search_type_decl = 1,
	search_typename = 2,
	search_member_decl = 3,
} search_kind_t;

/*
 * if/how a name string is an elaborated type
 *
 * I.e., search for any type named "foo", or struct "foo".
 *
 * XXX terrible names
 */
typedef enum {
	name_none = 1,
	name_struct = 2,
	name_union = 3,
	name_enum = 4,
} name_elab_t;

typedef struct {
	name_elab_t kind;
	cf_str_t name;
} name_spec_t;

/*
 */
typedef struct {
	bool is_id;
	union {
		int64_t rowid;
		name_spec_t name;
	};
} type_search_t;

/*
 */
typedef struct {
	name_spec_t name;
} typename_search_t;

/*
 */
typedef struct {
	type_search_t base;
	cf_str_t name;
} member_search_t;

typedef struct {
	search_kind_t kind;
	bool test_option;
	union {
		type_search_t type;
		typename_search_t typename;
		member_search_t member;
	} arg;
} search_cmd_t;

void free_search_cmd(search_cmd_t *cmd);
type_kind_t elab2type_kind(name_elab_t elab);

__END_DECLS
