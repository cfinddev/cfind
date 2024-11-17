/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * In-memory database backend
 */
#pragma once

#include "cc_support.h"
#include "cf_string.h"
#include "cf_vector.h"
#include "db_types.h"

#include <stdint.h>
#include <string.h>

__BEGIN_DECLS

CF_VEC_TYPE_DECL(file_vec_t, cf_str_t);
CF_VEC_TYPE_DECL(type_vec_t, db_type_entry_t);
CF_VEC_TYPE_DECL(typename_vec_t, db_typename_t);
CF_VEC_TYPE_DECL(member_vec_t, db_member_t);
CF_VEC_TYPE_DECL(type_use_vec_t, db_type_use_t);
CF_VEC_TYPE_DECL(loc_vec_t, loc_ctx_t);

#define MEM_DB_NUM_VEC 4

/*
 * In-memory database.
 *
 * Only used for tests.
 *
 * Members
 * - files
 *   Source containing files.
 * - user_types
 *   User defined types: structs, unions, enums only.
 * - typenames
 *   Typedefs and names of `user_types`. structs, etc. only. No entries for
 *   builtin types.
 * - members
 *   Struct/union members of `user_types`.
 * - type_uses
 *   Miscellaneous uses of types in `user_types`. The whole type is involved,
 *   rather than just an individual member.
 */
typedef struct {
	file_vec_t files;
	type_vec_t user_types;
	typename_vec_t typenames;
	member_vec_t members;
	type_use_vec_t type_uses;
	loc_vec_t locs[MEM_DB_NUM_VEC];
} mem_db_t;

/*
 * Typname iterator implementation.
 *
 * Members
 * - i
 *   Current index into `mem_db_t::typenames`.
 * - key
 *   The name string being searched for.
 */
typedef struct {
	size_t i;
	cf_str_t key;
} mem_db_typename_iter_t;

int mem_db_open(mem_db_t *db);
int mem_db_close(mem_db_t *db);
int mem_db_add_file(mem_db_t *db, const char *path, size_t len, size_t *out);

int mem_db_typename_lookup(mem_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *name, size_t *out);
int mem_db_type_insert(mem_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, size_t *out);
int mem_db_typename_insert(mem_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry);
int mem_db_member_insert(mem_db_t *db, const loc_ctx_t *loc,
		const db_member_t *entry);
int mem_db_type_use_insert(mem_db_t *db, const loc_ctx_t *loc,
		const db_type_use_t *entry);

int mem_db_file_lookup(mem_db_t *db, size_t id, cf_str_t *out);
int mem_db_type_lookup(mem_db_t *db, size_t id,
		db_type_entry_t *entry_out, loc_ctx_t *loc_out);
int mem_db_member_lookup(mem_db_t *db, size_t parent, const cf_str_t *name,
		db_member_t *entry_out, loc_ctx_t *loc_out);
int mem_db_typename_find(mem_db_t *db, const cf_str_t *name,
		mem_db_typename_iter_t *out);

void mem_db_typename_iter_free(mem_db_typename_iter_t *it);
void mem_db_typename_iter_peek(const mem_db_t *db,
		const mem_db_typename_iter_t *it, db_typename_t *entry_out,
		loc_ctx_t *loc_out);
bool mem_db_typename_iter_next(mem_db_t *db, mem_db_typename_iter_t *it);

__END_DECLS
