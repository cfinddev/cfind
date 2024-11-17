/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Database backends.
 */
#pragma once

#include "cc_support.h"
#include "db_types.h"
#include "nop_db.h"
#include "mem_db.h"
#include "sql_db.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

__BEGIN_DECLS

/*
 * Database frontend interface.
 *
 * This static dispatches between different database backends implementations.
 * It currently supports a nop, sqlite, and an in-memory database.
 *
 * Members:
 * - db_kind
 *   Tag that specifies what type of backend this is. This indicates which
 *   union member is in use.
 * - <anonymous union>
 *   Database members. A `db_kind` with value:
 *   - `kind_nop` uses `nop`
 *   - `kind_mem` uses `mem`
 *   - `kind_sql` uses `sql`
 */
typedef struct {
	enum __attribute__((enum_extensibility(closed))) {
		db_kind_nop = 1,
		db_kind_mem = 2,
		db_kind_sql = 3,
	} db_kind;
	union {
		nop_db_t nop;
		mem_db_t mem;
		sqlite_db_t sql;
	};
} cf_db_t;

/*
 * Iterator used to return results from a typename find query.
 *
 * In a manner similar to `cf_db_t`, this serves to static dispatch between
 * different database backends' iterators.
 *
 * Use is like other iterators, except that cf_db_typename_find(), rather
 * than "_make", is the function to create a new iterator.
 * Example use:
 *   cf_db_t db = ...;
 *   cf_str_t name = ...;
 *   db_typename_iter_t it;
 *
 *   (void)cf_db_typename_find(&db, name, &it);
 *
 *   while (db_typename_iter_next(&it)) {
 *     ... db_typename_iter_peek(&it);
 *     // do something with the peeked value
 *   }
 *   db_typename_iter_free(&it);
 *
 * Members:
 * - parent
 *   Database
 * - <anonymous union>
 *   Implementation specific iterator state per kind of database.
 *   The selector for the active union variant is `parent->db_kind`.
 * - entry
 *   The current typename.
 * - loc
 *   The current source location.
 */
typedef struct {
	cf_db_t *parent;
	union {
		nop_db_typename_iter_t nop;
		mem_db_typename_iter_t mem;
		sqlite_db_typename_iter_t sql;
	};
} db_typename_iter_t;

// creation
int cf_db_open_nop(cf_db_t *out);
int cf_db_open_mem(cf_db_t *out);
int cf_db_open_sql(const char *db_path, bool ro, cf_db_t *out);
int cf_db_close(cf_db_t *db);

// virtual interface functions
int cf_db_add_file(cf_db_t *db, const char *path, size_t len,
		file_ref_t *out);
int cf_db_add_typedef(cf_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry);

int cf_db_typename_lookup(cf_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *name, type_ref_t *out);
int cf_db_type_insert(cf_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, type_ref_t *out);
int cf_db_typename_insert(cf_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry);
int cf_db_member_insert(cf_db_t *db, const loc_ctx_t *loc,
		const db_member_t *entry);
int cf_db_type_use_insert(cf_db_t *db, const loc_ctx_t *loc,
		const db_type_use_t *entry);

int cf_db_file_lookup(cf_db_t *db, file_ref_t id, cf_str_t *out);
int cf_db_type_lookup(cf_db_t *db, type_ref_t id, db_type_entry_t *entry_out,
		loc_ctx_t *loc_out);
int cf_db_member_lookup(cf_db_t *db, type_ref_t parent,
		const cf_str_t *member, db_member_t *entry_out, loc_ctx_t *loc_out);
int cf_db_typename_find(cf_db_t *db, const cf_str_t *name,
		db_typename_iter_t *out);

void db_typename_iter_free(db_typename_iter_t *it);
void db_typename_iter_peek(const db_typename_iter_t *it,
		db_typename_t *entry_out, loc_ctx_t *loc_out);
bool db_typename_iter_next(db_typename_iter_t *it);

__END_DECLS
