/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * The point of this file, compared to "sql_query.h", is to
 * - provide a slightly higher level interface
 * - normalize input and output to the sql database
 */
#define _POSIX_C_SOURCE 200809L // for realpath(3)
#define _XOPEN_SOURCE 700
#include "sql_db.h"

#include "sql_query.h"
#include "cf_alloc.h"
#include "cf_assert.h"
#include "cf_print.h"
#include "cf_vector.h"

#include <errno.h>
#include <sys/param.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

// value of `sql_db_t::buf_len`
#define SQL_DB_BUF_LEN PATH_MAX

static int clean_path(sqlite_db_t *db, const char *path_in, size_t len,
		const char **out);

static bool sanitize_typename(const db_typename_t *name);
static bool sanitize_typename_kind(uint32_t kind);
static bool sanitize_rowid(int64_t rowid);

/*
 * Initialize a `sqlite_db_t`.
 *
 * Steps:
 * - allocate buffers for calls to realpath(3)
 * - open database at `db_path`
 */
int
sql_db_open(const char *db_path, bool ro, sqlite_db_t *out)
{
	int error;

	cf_print_info("sql_db_open(db_path='%s', ro=%d)\n", db_path, ro);

	memset(out, 0, sizeof(*out));
	out->readonly = ro;
	const size_t buf_len = out->buf_len = SQL_DB_BUF_LEN;

	if (!(out->path_buf[0] = cf_malloc(buf_len))) {
		error = ENOMEM;
		goto fail;
	}

	if (!(out->path_buf[1] = cf_malloc(buf_len))) {
		error = ENOMEM;
		goto fail_alloc;
	}

	if ((error = sql_open(db_path, ro, &out->sql))) {
		cf_print_err("cannot open sql db '%s', error %d\n", db_path, error);
		goto fail_open;
	}

	cf_assert(out->sql);
	cf_assert(out->path_buf[0]);
	cf_assert(out->path_buf[1]);
	return 0;
fail_open:
	cf_free(out->path_buf[1]);
fail_alloc:
	cf_free(out->path_buf[0]);
fail:
	cf_assert(error);
	return error;
}

/*
 * Free a `sqlite_db_t` returned from a previous call to sql_db_open().
 *
 * Steps:
 * - free underlying `sql` handle
 * - free realpath buffers
 */
int
sql_db_close(sqlite_db_t *db)
{
	cf_print_debug("flushing sqlite db\n");
	(void)sqlite3_close(db->sql);
	cf_free(db->path_buf[0]);
	cf_free(db->path_buf[1]);
	return 0;
}

/*
 * Insert a new entry for a source-containing file.
 *
 * `path` is the filesystem path of the file, `len` bytes in length. It need
 * not be NUL terminated. `out` will be set to a reference to the file that
 * other database entries can use to indicate they're contained in this file.
 *
 * Note:
 * - reinserting the same file is not an error
 *   In that case ,this function does nothing and sets `out` to the preexisting
 *   id.
 * - `path` is not a unique value
 *   many filesystem paths can map to the same underlying file
 *   this function handles some cases (like excess '/'s) but not all (like
 *   hardlinks)
 *
 * XXX the current implemntation stores absolute paths on disk. Ideally,
 * project root-relative paths should be store but that's harder to implement.
 *
 * Steps:
 * - clean `path`
 * - lookup any preexisting file
 * - insert new entry
 */
int
sql_db_add_file(sqlite_db_t *db, const char *path_, size_t len_,
		int64_t *out)
{
	cf_assert(len_ < INT_MAX);
	int error;
	size_t len;

	if (db->readonly) {
		return EACCES;
	}

	cf_print_info("clean path %zu-byte '%.*s'\n", len_, (int)len_, path_);
	// clean and NUL-terminate `path`
	// (also filter out non-files)
	const char *path;
	if ((error = clean_path(db, path_, len_, &path))) {
		goto fail;
	}
	len = strnlen(path, db->buf_len);
	cf_print_info("path cleaned to %zu-byte '%s'\n", len, path);

	// the cleaned path should still exist
	if (access(path, F_OK) == -1) {
		error = errno;
		goto fail;
	}

	// check sql db for preexistence
	error = lookup_file(db->sql, path, len, out);

	if (!error) {
		goto fail;
	}

	if (error != ENOENT) {
		// some other error happened during lookup
		// we can't tell if `path` is new
		cf_print_debug("cannot look up file '%s', error %d\n", path, error);
		goto fail;
	}

	// it doesn't exist, insert it, save rowid
	if ((error = insert_file(db->sql, path, len, out))) {
		cf_print_debug("cannot insert file '%s', error %d\n", path, error);
		goto fail;
	}

fail:
	return error;
}

int
sql_db_typename_lookup(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *name, int64_t *out)
{
	cf_assert(!cf_str_is_null(&name->name));
	return lookup_typename(db->sql, loc, name, out);
}

int
sql_db_type_insert(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, int64_t *out)
{
	cf_assert(entry->complete);

	if (db->readonly) {
		return EACCES;
	}

	return insert_complete_type(db->sql, loc, entry, out);
	// XXX on success, consider tracking `rowid` to make sure a future
	// _typename_insert() references it
}

int
sql_db_typename_insert(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry)
{
	if (db->readonly) {
		return EACCES;
	}

	int64_t dummy;
	return insert_typename(db->sql, loc, entry, &dummy);
	// XXX consider checking whether this typename is the first name for a
	// type entry
	// see above
}

int
sql_db_type_use_insert(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_type_use_t *entry)
{
	if (db->readonly) {
		return EACCES;
	}

	int64_t dummy;
	return insert_type_use(db->sql, loc, entry, &dummy);
}

int
sql_db_member_insert(sqlite_db_t *db, const loc_ctx_t *loc,
		const db_member_t *entry)
{
	if (db->readonly) {
		return EACCES;
	}

	int64_t dummy;
	return insert_member(db->sql, loc, entry, &dummy);
}

int
sql_db_file_lookup(sqlite_db_t *db, int64_t rowid, cf_str_t *out)
{
	cf_assert(rowid);
	return lookup_file_id(db->sql, rowid, out);
}

int
sql_db_type_lookup(sqlite_db_t *db, int64_t rowid, db_type_entry_t *entry_out,
		loc_ctx_t *loc_out)
{
	cf_assert(rowid);

	return lookup_type_entry(db->sql, rowid, entry_out, loc_out);
}

int
sql_db_member_lookup(sqlite_db_t *db, int64_t parent, const cf_str_t *member,
		db_member_t *entry_out, loc_ctx_t *loc_out)
{
	cf_assert(parent);

	return lookup_member(db->sql, parent, member, entry_out, loc_out);
}

int
sql_db_typename_find(sqlite_db_t *db, const cf_str_t *name,
		sqlite_db_typename_iter_t *out)
{
	int error;

	memset(out, 0, sizeof(*out));

	if ((error = find_typenames(db->sql, name, &out->stmt))) {
		goto fail;
	}

fail:
	return error;
}

void
sql_db_typename_iter_free(sqlite_db_typename_iter_t *it)
{
	free_typenames(it->stmt);
}

void
sql_db_typename_iter_peek(const sqlite_db_t *db,
		const sqlite_db_typename_iter_t *it, db_typename_t *entry_out,
		loc_ctx_t *loc_out)
{
	(void)db;
	memcpy(entry_out, &it->cur_name, sizeof(*entry_out));
	memcpy(loc_out, &it->cur_loc, sizeof(*loc_out));
}

bool
sql_db_typename_iter_next(sqlite_db_t *db, sqlite_db_typename_iter_t *it)
{
	int error;

	// stop borrowing name (a nop) before mutating `it->stmt`
	cf_str_free(&it->cur_name.name);

	// advance iterator
	if ((error = iter_next_typename(it->stmt))) {
		cf_print_info("typename iterator %p ended with %d\n", it, error);
		return false;
	}

	// deserialize
	if ((error = iter_get_typename(it->stmt, &it->cur_name, &it->cur_loc))) {
		cf_print_err("can't deserialize typename iter %p, error %d\n",
				it, error);
		return false;
	}

	// sanitize
	if (!sanitize_typename(&it->cur_name)) {
		cf_print_corrupt("deserialized corrupt typename\n");
		return false;
	}

	return true;
}

/*
 * Clean `path_in` and copy it to NUL-terminated `*out`.
 *
 * `*out` is borrowed from `db`. It need not be explicitly freed; a call to
 * sql_db_close() does that.
 */
static int
clean_path(sqlite_db_t *db, const char *path_in, size_t len, const char **out)
{
	// check: len+1 <= buf_len
	if (len >= db->buf_len) {
		return ERANGE;
	}
	memcpy(db->path_buf[0], path_in, len);
	db->path_buf[0][len] = '\0';

	// XXX returns an absolute path
	// not exactly suitable but maybe good enough for now
	if (!realpath(db->path_buf[0], db->path_buf[1])) {
		return errno;
	}

	*out = db->path_buf[1];
	return 0;
}

static bool
sanitize_typename(const db_typename_t *name)
{
	if (!sanitize_typename_kind(name->kind)) {
		cf_print_info("bad kind %u\n", name->kind);
		return false;
	}

	const int64_t rowid = name->base_type.rowid;
	if (!sanitize_rowid(rowid)) {
		cf_print_info("bad base-type %lld\n", p_(rowid));
		return false;
	}

	if (cf_str_is_null(&name->name)) {
		cf_print_info("bad name\n");
		return false;
	}

	return true;
}

static bool
sanitize_typename_kind(uint32_t kind)
{
	switch (kind) {
		case name_kind_direct:
		case name_kind_typedef:
		case name_kind_var:
			return true;
	}
	return false;
}

static bool
sanitize_rowid(int64_t rowid)
{
	return (0 < rowid) && (rowid < INT64_MAX);
}
