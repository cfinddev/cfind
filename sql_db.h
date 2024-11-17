/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * sqlite3 database backend
 */
#pragma once

#include "cc_support.h"
#include "cf_map.h"
#include "cf_vector.h"
#include "db_types.h"

#include <sqlite3.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

__BEGIN_DECLS

/*
 * Sqlite database backend.
 *
 * Members
 * - sql
 *   database connection handle
 * - readonly
 *   The modifiability of the database. With value:
 *   - true
 *     The database is readonly. Attempts to modify it (e.g., a call to
 *     sql_db_add_type()) will fail.
 *   - false
 *     The database is expected to be modified. A transaction is entered within
 *     sql_db_open() for anticipated, future modifications.
 * - buf_len
 *   Length, in bytes, of each buffer in `path_buf`.
 * - path_buf
 *   Two heap-allocated buffers for passing as input and output to realpath(3).
 */
typedef struct {
	sqlite3 *sql;
	bool readonly;
	size_t buf_len;
	char *path_buf[2];
} sqlite_db_t;

/*
 * Typename iterator implementation.
 *
 * See interface type `db_typename_iter_t` in "cf_db.h".
 *
 * Members
 * - stmt
 *   sqlite3 statement that selects typename entries matching a string.
 * - cur_name
 *   Current name entry. The `name` member is a string that borrows from
 *   `stmt`. Advancing `stmt` invalidates `cur_name`.
 * - cur_loc
 *   Current location.
 */
typedef struct {
	sqlite3_stmt *stmt;
	db_typename_t cur_name;
	loc_ctx_t cur_loc;
} sqlite_db_typename_iter_t;

int sql_db_open(const char *db_path, bool ro, sqlite_db_t *out);
int sql_db_close(sqlite_db_t *db);
int sql_db_add_file(sqlite_db_t *db, const char *path, size_t len,
		int64_t *out);

int sql_db_typename_lookup(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *name, int64_t *out);
int sql_db_type_insert(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, int64_t *out);
int sql_db_typename_insert(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry);
int sql_db_type_use_insert(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_type_use_t *entry);
int sql_db_member_insert(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_member_t *entry);

int sql_db_file_lookup(sqlite_db_t *db, int64_t rowid, cf_str_t *out);
int sql_db_type_lookup(sqlite_db_t *db, int64_t rowid,
		db_type_entry_t *entry_out, loc_ctx_t *loc_out);
int sql_db_member_lookup(sqlite_db_t *db, int64_t parent,
		const cf_str_t *member, db_member_t *entry_out, loc_ctx_t *loc_out);
int sql_db_typename_find(sqlite_db_t *db, const cf_str_t *name,
		sqlite_db_typename_iter_t *out);

void sql_db_typename_iter_free(sqlite_db_typename_iter_t *it);
void sql_db_typename_iter_peek(const sqlite_db_t *db,
		const sqlite_db_typename_iter_t *it, db_typename_t *entry_out,
		loc_ctx_t *loc_out);
bool sql_db_typename_iter_next(sqlite_db_t *db, sqlite_db_typename_iter_t *it);

__END_DECLS
