/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "search_types.h"

#include "cf_assert.h"
#include "cf_string.h"

/*
 * Free internal resources held by a `search_cmd_t` initialized from a previous
 * call to parse_command().
 */
void
free_search_cmd(search_cmd_t *cmd)
{
	switch (cmd->kind) {
		case search_type_decl: {
			type_search_t *arg = &cmd->arg.type;
			if (!arg->is_id) {
				cf_str_free(&arg->name.name);
			}
			break;
		}
		case search_typename: {
			typename_search_t *arg = &cmd->arg.typename;
			cf_str_free(&arg->name.name);
			break;
		}
		case search_member_decl: {
			member_search_t *arg = &cmd->arg.member;
			cf_str_free(&arg->name);
			break;
		}
		default:
			__builtin_unreachable();
	}
}

/*
 * Convert `name_elab_t` into the corresponding `type_kind_t` value.
 *
 * Don't pass in `name_none`; it cannot be converted.
 */
type_kind_t
elab2type_kind(name_elab_t elab)
{
	cf_assert(elab != name_none);
	switch (elab) {
		case name_none:
			__builtin_trap();
		case name_struct:
			return type_kind_struct;
		case name_union:
			return type_kind_union;
		case name_enum:
			return type_kind_enum;
	}
	__builtin_unreachable();
}
