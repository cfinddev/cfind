/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "db_types.h"

/*
 * The following functions convert an enum, `kind`, to its string
 * representation.
 *
 * These are useful for printing.
 *
 * Each returned pointer is a string literal; it does not need to be freed.
 */

const char *
db_type_kind_str(type_kind_t kind)
{
	switch (kind) {
		case type_kind_struct:
			return "struct";
		case type_kind_union:
			return "union";
		case type_kind_enum:
			return "enum";
	}
}

const char *
db_member_access_str(member_access_kind_t kind)
{
	switch (kind) {
		case member_access_load:
			return "load";
		case member_access_store:
			return "store";
		case member_access_rmw:
			return "rmw";
		case member_access_loc:
			return "&";
	}
}

const char *
db_type_use_str(type_use_kind_t kind)
{
	switch (kind) {
		case type_use_decl:
			return "decl";
		case type_use_init:
			return "init";
		case type_use_param:
			return "param";
		case type_use_cast:
			return "cast";
		case type_use_sizeof:
			return "sizeof";
	}
}
