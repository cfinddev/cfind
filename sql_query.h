/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Low level interface for sql queries.
 */
#pragma once

#include "cc_support.h"
#include "db_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sqlite3.h>

__BEGIN_DECLS

int sql_open(const char *db_path, bool ro, sqlite3 **sql_out);

int lookup_file(sqlite3 *db, const char *path, size_t len, int64_t *rowid_out);
int lookup_file_id(sqlite3 *db, int64_t rowid, cf_str_t *out);
int insert_file(sqlite3 *db, const char *path, size_t len, int64_t *rowid_out);

int insert_complete_type(sqlite3 *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, int64_t *rowid_out);
int insert_typename(sqlite3 *db, const loc_ctx_t *loc,
		const db_typename_t *name, int64_t *rowid_out);
int insert_type_use(sqlite3 *db, const loc_ctx_t *loc,
		const db_type_use_t *entry, int64_t *rowid_out);
int insert_member(sqlite3 *db, const loc_ctx_t *loc, const db_member_t *entry,
		int64_t *rowid_out);

int lookup_type_entry(sqlite3 *db, int64_t rowid, db_type_entry_t *entry_out,
		loc_ctx_t *loc_out);
int lookup_typename(sqlite3 *db, const loc_ctx_t *loc,
		const db_typename_t *name, int64_t *rowid_out);
int lookup_member(sqlite3 *db, int64_t parent, const cf_str_t *member,
		db_member_t *entry_out, loc_ctx_t *loc_out);

// typename iterator
int find_typenames(sqlite3 *db, const cf_str_t *name, sqlite3_stmt **out);
int iter_next_typename(sqlite3_stmt *stmt);
int iter_get_typename(sqlite3_stmt *stmt, db_typename_t *entry_out,
		loc_ctx_t *loc_out);
void free_typenames(sqlite3_stmt *stmt);

__END_DECLS
