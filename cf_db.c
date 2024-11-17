/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "cf_db.h"
#include "mem_db.h"
#include "sql_db.h"
#include "cf_assert.h"

#include <errno.h>
#include <string.h>
#include <stdbool.h>

/*
 * API
 */

int
cf_db_open_nop(cf_db_t *out)
{
	memset(out, 0, sizeof(*out));
	out->db_kind = db_kind_nop;
	return nop_db_open(&out->nop);
}

int
cf_db_open_mem(cf_db_t *out)
{
	memset(out, 0, sizeof(*out));
	out->db_kind = db_kind_mem;
	return mem_db_open(&out->mem);
}

int
cf_db_open_sql(const char *db_path, bool ro, cf_db_t *out)
{
	memset(out, 0, sizeof(*out));
	out->db_kind = db_kind_sql;
	return sql_db_open(db_path, ro, &out->sql);
}

/*
 * Free a database created from a previous successful _open() call.
 *
 * Each of
 * - cf_db_open_nop()
 * - cf_db_open_mem()
 * - cf_db_open_sql()
 * need to be followed with a call to this function.
 */
int
cf_db_close(cf_db_t *db)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return 0;
		case db_kind_mem:
			return mem_db_close(&db->mem);
		case db_kind_sql:
			return sql_db_close(&db->sql);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

/*
 * Insert a path to a file into `db`.
 *
 * On success, a reference to the file is returned via `out`. This function
 * succeeds if either the file is new, or the file preexists.
 *
 * Note: although `path` is a filesystem path, it need not be NUL terminated.
 * It is `len` bytes *excluding* any terminator.
 */
int
cf_db_add_file(cf_db_t *db, const char *path, size_t len, file_ref_t *out)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_add_file(&db->nop, path, len, &out->rowid);
		case db_kind_mem:
			return mem_db_add_file(&db->mem, path, len, &out->index);
		case db_kind_sql:
			return sql_db_add_file(&db->sql, path, len, &out->rowid);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

/*
 * Look up a typename matching `name` and `loc`.
 *
 * If a matching name preexists, a reference is returned via `out`. If not,
 * ENOENT is returned. Other errors may be returned if the lookup fails to
 * complete.
 *
 * The bits checked for a match are:
 * - loc->file
 * - loc->scope
 *   (XXX unimplemented)
 * - name->name
 * - name->kind
 */
int
cf_db_typename_lookup(cf_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *name, type_ref_t *out)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_typename_lookup(&db->nop, loc, name, &out->rowid);
		case db_kind_mem:
			return mem_db_typename_lookup(&db->mem, loc, name, &out->index);
		case db_kind_sql:
			return sql_db_typename_lookup(&db->sql, loc, name, &out->rowid);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

/*
 * Insert a new type described by `entry` and `loc`.
 *
 * This function only inserts a type entry. It's up to the caller to call
 * cf_db_typename_insert() to add a typename that references `out`.
 *
 * On success, a reference to the type is returned via `out`.
 */
int
cf_db_type_insert(cf_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, type_ref_t *out)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_type_insert(&db->nop, loc, entry, &out->rowid);
		case db_kind_mem:
			return mem_db_type_insert(&db->mem, loc, entry, &out->index);
		case db_kind_sql:
			return sql_db_type_insert(&db->sql, loc, entry, &out->rowid);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

int
cf_db_typename_insert(cf_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_typename_insert(&db->nop, loc, entry);
		case db_kind_mem:
			return mem_db_typename_insert(&db->mem, loc, entry);
		case db_kind_sql:
			return sql_db_typename_insert(&db->sql, loc, entry);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

int
cf_db_member_insert(cf_db_t *db, const loc_ctx_t *loc,
		const db_member_t *entry)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_member_insert(&db->nop, loc, entry);
		case db_kind_mem:
			return mem_db_member_insert(&db->mem, loc, entry);
		case db_kind_sql:
			return sql_db_member_insert(&db->sql, loc, entry);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

int
cf_db_type_use_insert(cf_db_t *db, const loc_ctx_t *loc,
		const db_type_use_t *entry)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_type_use_insert(&db->nop, loc, entry);
		case db_kind_mem:
			return mem_db_type_use_insert(&db->mem, loc, entry);
		case db_kind_sql:
			return sql_db_type_use_insert(&db->sql, loc, entry);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

/*
 * Resolve unique file identifier `id` to a file entry.
 *
 * On success, the path to the file is returned via `out`.
 */
int
cf_db_file_lookup(cf_db_t *db, file_ref_t id, cf_str_t *out)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_file_lookup(&db->nop, id.rowid, out);
		case db_kind_mem:
			return mem_db_file_lookup(&db->mem, id.index, out);
		case db_kind_sql:
			return sql_db_file_lookup(&db->sql, id.rowid, out);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);

}

int
cf_db_type_lookup(cf_db_t *db, type_ref_t id, db_type_entry_t *entry_out,
		loc_ctx_t *loc_out)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_type_lookup(&db->nop, id.rowid,
					entry_out, loc_out);
		case db_kind_mem:
			return mem_db_type_lookup(&db->mem, id.index,
					entry_out, loc_out);
		case db_kind_sql:
			return sql_db_type_lookup(&db->sql, id.rowid,
					entry_out, loc_out);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

/*
 * Look up a member of struct/union `parent` with name matching `member`.
 *
 * On success, return the entry via `*entry_out` and `*loc_out`. The returned
 * entry contains an owned string of the full member name. Call cf_str_free()
 * on `&entry_out->name`.
 */
int
cf_db_member_lookup(cf_db_t *db, type_ref_t parent, const cf_str_t *member,
		db_member_t *entry_out, loc_ctx_t *loc_out)
{
	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_member_lookup(&db->nop, parent.rowid, member,
					entry_out, loc_out);
		case db_kind_mem:
			return mem_db_member_lookup(&db->mem, parent.index, member,
					entry_out, loc_out);
		case db_kind_sql:
			return sql_db_member_lookup(&db->sql, parent.rowid, member,
					entry_out, loc_out);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

/*
 * Do a lookup for `name` and return an iterator of matching entries.
 *
 * On success, follow with a call to db_typname_iter_free().
 *
 * Even if no typenames match `name`, an empty iterator should be successfully
 * created. The next db_typename_iter_next() call will return false. See the
 * docs above `db_typename_iter_t` for more details on use.
 *
 * `name` is borrowed. It needs to live until `*out` is freed.
 */
int
cf_db_typename_find(cf_db_t *db, const cf_str_t *name,
		db_typename_iter_t *out)
{
	memset(out, 0, sizeof(*out));
	out->parent = db;

	switch (db->db_kind) {
		case db_kind_nop:
			return nop_db_typename_find(&db->nop, name, &out->nop);
		case db_kind_mem:
			return mem_db_typename_find(&db->mem, name, &out->mem);
		case db_kind_sql:
			return sql_db_typename_find(&db->sql, name, &out->sql);
	}
	cf_panic("unknown database impl %d\n", db->db_kind);
}

/*
 * Free an iterator made by cf_db_typename_find().
 */
void
db_typename_iter_free(db_typename_iter_t *it)
{
	switch (it->parent->db_kind) {
		case db_kind_nop:
			return nop_db_typename_iter_free(&it->nop);
		case db_kind_mem:
			return mem_db_typename_iter_free(&it->mem);
		case db_kind_sql:
			return sql_db_typename_iter_free(&it->sql);
	}
	cf_panic("unknown database impl %d\n", it->parent->db_kind);
}

/*
 * Return the current typename entry in `it`.
 *
 * `entry_out`, or more specifically `entry_out->name`, is borrowed from `it`.
 * It is only valid until the next db_typename_iter_next() or
 * db_typename_iter_free() call.
 *
 * This function cannot fail. The iterator must currently be on an entry. This
 * is the case when the previous db_typename_iter_next() call returned true.
 */
void
db_typename_iter_peek(const db_typename_iter_t *it, db_typename_t *entry_out,
		loc_ctx_t *loc_out)
{
	switch (it->parent->db_kind) {
		case db_kind_nop:
			return nop_db_typename_iter_peek(&it->parent->nop, &it->nop,
					entry_out, loc_out);
		case db_kind_mem:
			return mem_db_typename_iter_peek(&it->parent->mem, &it->mem,
					entry_out, loc_out);
		case db_kind_sql:
			return sql_db_typename_iter_peek(&it->parent->sql, &it->sql,
					entry_out, loc_out);
	}
	cf_panic("unknown database impl %d\n", it->parent->db_kind);
}

/*
 * Advance the iterator to the next typename.
 *
 * Return true on success. Regardless of the return value, any entry returned
 * via a db_typename_iter_peek() call is invalidated.
 */
bool
db_typename_iter_next(db_typename_iter_t *it)
{
	switch (it->parent->db_kind) {
		case db_kind_nop:
			return nop_db_typename_iter_next(&it->parent->nop, &it->nop);
		case db_kind_mem:
			return mem_db_typename_iter_next(&it->parent->mem, &it->mem);
		case db_kind_sql:
			return sql_db_typename_iter_next(&it->parent->sql, &it->sql);
	}
	cf_panic("unknown database impl %d\n", it->parent->db_kind);
}
