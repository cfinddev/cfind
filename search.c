/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "search.h"

#include "search_types.h"
#include "parse.h"
#include "cf_string.h"
#include "cf_print.h"
#include "cf_assert.h"
#include "db_types.h"
#include "cf_db.h"
#include "sql_db.h"
#include "token.h"

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static int exec_search(cf_db_t *db, search_cmd_t *cmd);
static int exec_search_type(cf_db_t *db, type_search_t *query);
static int search_type_core(cf_db_t *db, type_search_t *query,
		type_ref_t *id_out, db_type_entry_t *entry_out, loc_ctx_t *loc_out);
static int exec_search_typename(cf_db_t *db, typename_search_t *query);
static int exec_search_member(cf_db_t *db, member_search_t *query);

static int find_one_type(cf_db_t *db, const name_spec_t *name,
		type_ref_t *out);
static int find_elab_type(cf_db_t *db, const name_spec_t *name,
		type_ref_t *out);

static int print_all_typenames(cf_db_t *db, const name_spec_t *name);

static void print_type_entry(type_ref_t id, db_type_entry_t *entry,
		loc_ctx_t *loc, const cf_str_t *file);
static void print_one_typename(db_typename_t *name, loc_ctx_t *loc,
		const cf_str_t *file);
static void print_member_entry(type_ref_t parent, const db_member_t *entry,
		const loc_ctx_t *loc, const cf_str_t *file);

/*
 * Print a user-facing message.
 */
#define user_print(fmt, ...) printf(fmt, ##__VA_ARGS__)

/*
 * parse `cmd` into a `search_cmd_t`, then pass it to another function to
 * "execute" a search query.
 *
 * ideas:
 * - search for type definition, get location back
 *
 * XXX
 */
int
run_one_command(const char *db_path, const cf_str_t *cmd)
{
	int error;
	cf_db_t db;

	// open `db_path`
	if ((error = cf_db_open_sql(db_path, false, &db))) {
		goto fail;
	}

	// parse `cmd` into a query struct
	search_cmd_t query;
	if ((error = parse_command(cmd, &query))) {
		goto fail_cmd;
	}

	// execute search query
	if ((error = exec_search(&db, &query))) {
		goto fail_search;
	}

fail_search:
	free_search_cmd(&query);
fail_cmd:
	cf_db_close(&db);
fail:
	return error;
}

#if 0
/*
 * XXX experimental
 * consider just directly printing database entries instead of returning a
 * generic any-query-iterator
 */
typedef struct {
	db_impl_t *parent;

	query_kind_t

	// iterator state specific to `kind`?
	union {
		// XXX use this for *both* typename and type-via-name
		db_typename_iter_t name_iter;
		db_member_iter_t member_iter;
	};

	// db-specific state
	union {

	};

	// kind of every entry returned
	db_obj_kind_t kind;
	// heap-allocated buffer for entry outputs
	size_t buf_capacity;
	void *buf;

	// location output
	loc_ctx_t loc_out;
} search_iter_t;
#endif // 0

/*
 * What should this return, if anything?
 * Should it just print? Should it return a buffer and let the caller print?
 *
 * if it just prints, or returns text, it might make it hard to use otherwise.
 * There wouldn't be any use for `search_cmd_t` outside of printing; i.e.,
 * making it hard to programatically test queries
 * aha, it could return an iterator
 * each iterator, it mutates a buffer and sets an enum
 * the only part about this that would be hard is the variable length string
 * name, but that can be stored in a separate char buf[], each type of db
 * object can be in a union
 * This gets trickier when it comes to loc_ctx_t. Sure you can make an iterator
 * that returns any db type from a query, but every reference type returned
 * needs to be resolved and then printed
 */
static int
exec_search(cf_db_t *db, search_cmd_t *cmd)
{
	switch (cmd->kind) {
		case search_type_decl:
			return exec_search_type(db, &cmd->arg.type);
		case search_typename:
			return exec_search_typename(db, &cmd->arg.typename);
		case search_member_decl:
			return exec_search_member(db, &cmd->arg.member);
	}
	__builtin_unreachable();
}

/*
 * Somehow use `query` to call into sqlite. Get back a sql cursor, store it in
 * `out`.
 *
 * The caller uses `out` to see how many types match the query:
 * - 0: no matching entry
 * - 1: use it
 * - 2: ambiguous
 * XXX ^^^ wrong
 *
 * do the following:
 * - query.id
 *   get rowid
 * - query.name
 *   name -> typename table -> rowid
 * - rowid -> type table -> entry
 */
static int
exec_search_type(cf_db_t *db, type_search_t *query)
{
	int error;

	type_ref_t id;
	db_type_entry_t entry;
	loc_ctx_t loc;

	if ((error = search_type_core(db, query, &id, &entry, &loc))) {
		goto fail;
	}

	// resolve `loc->file` to its name
	cf_str_t file_name;
	if ((error = cf_db_file_lookup(db, loc.file, &file_name))) {
		goto fail;
	}

	print_type_entry(id, &entry, &loc, &file_name);

	cf_str_free(&file_name);
fail:
	return error;
}

static int
exec_search_typename(cf_db_t *db, typename_search_t *query)
{
	print_all_typenames(db, &query->name);

	return 0;
}

static int
exec_search_member(cf_db_t *db, member_search_t *query)
{
	int error;

	type_ref_t parent_id;
	db_type_entry_t type_entry;
	loc_ctx_t type_loc_; // dummy

	db_member_t member_entry;
	loc_ctx_t member_loc;

	// look up query->base, get type ID
	if ((error = search_type_core(db, &query->base, &parent_id,
			&type_entry, &type_loc_))) {
		goto fail;
	}

	// look up (type-ID, member-name)
	if ((error = cf_db_member_lookup(db, parent_id, &query->name,
			&member_entry, &member_loc))) {
		cf_print_err("lookup member id %lld '%.*s' error %d\n",
				p_(parent_id.rowid),
				(int)cf_str_len(&query->name), query->name.str,
				error);
		goto fail;
	}

	// resolve `member_loc->file` to its name
	cf_str_t file_name;
	if ((error = cf_db_file_lookup(db, member_loc.file, &file_name))) {
		goto fail_file;
	}

	print_member_entry(parent_id, &member_entry, &member_loc, &file_name);

	cf_str_free(&file_name);
fail_file:
	cf_str_free(&member_entry.name);
fail:
	return error;
}

static int
search_type_core(cf_db_t *db, type_search_t *query, type_ref_t *id_out,
		db_type_entry_t *entry_out, loc_ctx_t *loc_out)
{
	int error;

	type_ref_t id;
	// get type's rowid
	if (query->is_id) {
		// directly look up a type with query->rowid
		id.rowid = query->rowid;
	} else {
		// do a typename lookup with `query->name` to get a rowid
		if ((error = find_one_type(db, &query->name, &id))) {
			if (error == ENOENT) {
				user_print("no matching type\n");
			} else if (error == EMLINK) {
				user_print("ambiguous typename\n");
				(void)print_all_typenames(db, &query->name);
			}
			goto fail;
		}
	}

	// resolve `id_out` to a type entry
	if ((error = cf_db_type_lookup(db, id, entry_out, loc_out))) {
		if (error == ENOENT) {
			user_print("no type matching id %lld\n", p_(id.rowid));
		} else {
			cf_print_err("lookup id %lld failed with %d\n",
					p_(id.rowid), error);
		}
		goto fail;
	}

	*id_out = id;

fail:
	return error;
}

/*
 * Do the following:
 * - make an iterator over typenames matching `name`
 * - extract 1 entry
 *   fail if the iterator is empty
 * - save the type ID
 * - loop over remaining entries
 *   if this entry has the same type ID, continue
 *   otherwise, fail, print "ambiguous" and print every entry
 *   XXX in caller
 *
 * ------
 * name
 * - elab
 *   - none
 *     look up name->name in typename table, get iterator
 *     0: ENOENT
 *     1: return rowid
 *     2+: check all entries match rowid, EMLINK if not
 *   - !none
 *     do same lookup, get iterator
 *     1: check typename.kind == direct, check type table kind matches
 *     XXX what to do if 'name' matches but not 'struct'?
 *     kjlkjsf hard because 'struct' is also a uniqueifier
 *     might need to check the whole iterator
 *     2+: ignore duplicate rowids, but error on duplicate 'struct name's with
 *       different rowids
 */
static int
find_one_type(cf_db_t *db, const name_spec_t *name, type_ref_t *out)
{
	if (name->kind != name_none) {
		return find_elab_type(db, name, out);
	}
	int error;
	db_typename_iter_t iter;

	type_ref_t id;
	db_typename_t entry;
	loc_ctx_t loc;

	// search typename table for entries matching `name->name`
	if ((error = cf_db_typename_find(db, &name->name, &iter))) {
		goto fail;
	}

	// extract the first entry
	if (!db_typename_iter_next(&iter)) {
		error = ENOENT;
		goto fail_iter;
	}
	db_typename_iter_peek(&iter, &entry, &loc);
	id.rowid = entry.base_type.rowid;

	// check remaining entries
	while (db_typename_iter_next(&iter)) {
		db_typename_iter_peek(&iter, &entry, &loc);

		if (entry.base_type.rowid != id.rowid) {
			// many names matching `name` referencing different types
			error = EMLINK;
			goto fail_iter;
		}
	}

	// on success
	out->rowid = id.rowid;

fail_iter:
	db_typename_iter_free(&iter);
fail:
	return error;
}

/*
 * XXX duplicated because too different
 *
 *   - !none
 *     do same lookup, get iterator
 *     1: check typename.kind == direct, check type table kind matches
 *     XXX what to do if 'name' matches but not 'struct'?
 *     kjlkjsf hard because 'struct' is also a uniqueifier
 *     might need to check the whole iterator
 *     2+: ignore duplicate rowids, but error on duplicate 'struct name's with
 *       different rowids
 */
static int
find_elab_type(cf_db_t *db, const name_spec_t *name, type_ref_t *out)
{
	cf_assert(name->kind != name_none);

	int error;
	db_typename_iter_t iter;

	db_typename_t name_entry;
	db_type_entry_t type_entry;
	loc_ctx_t loc;
	loc_ctx_t type_loc;

	type_ref_t id = {
		.rowid = 0,
	};

	// search typename table for entries matching `name->name`
	if ((error = cf_db_typename_find(db, &name->name, &iter))) {
		goto fail;
	}

	// linear search all matches
	while (db_typename_iter_next(&iter)) {
		db_typename_iter_peek(&iter, &name_entry, &loc);

		// ignore non-elaborated typenames
		if (name_entry.kind != name_kind_direct) {
			continue;
		}

		// look up type-entry to check kind (struct, union, enum) matches
		if ((error = cf_db_type_lookup(db, name_entry.base_type,
				&type_entry, &type_loc))) {
			cf_print_corrupt("no type entry for %lld, error %d\n",
					p_(id.rowid), error);
			goto fail_iter;
		}

		if (elab2type_kind(name->kind) != type_entry.kind) {
			// different kind: `struct foo` != `union foo`
			continue;
		}

		if (!id.rowid) {
			// first match, save type ID
			id.rowid = name_entry.base_type.rowid;
		}

		if (name_entry.base_type.rowid != id.rowid) {
			// many names matching `name` referencing different types
			error = EMLINK;
			goto fail_iter;
		}
	}

	if (!id.rowid) {
		// no matches
		error = ENOENT;
		goto fail_iter;
	}

	// on success, return type ID
	*out = id;

fail_iter:
	db_typename_iter_free(&iter);
fail:
	return error;
}

/*
 * Look up and print all typenames matching `name`.
 *
 * XXX doesn't properly implement 'struct' name searches
 */
static int
print_all_typenames(cf_db_t *db, const name_spec_t *name)
{
	int error;
	db_typename_iter_t iter;

	db_typename_t entry;
	loc_ctx_t loc;

	// search typename table for entries matching `name`
	if ((error = cf_db_typename_find(db, &name->name, &iter))) {
		goto fail;
	}

	// print each entry
	while (db_typename_iter_next(&iter)) {
		// XXX change to check for name->kind
		// don't print types that 

		db_typename_iter_peek(&iter, &entry, &loc);

		// resolve `loc->file` to its name
		cf_str_t file_name;
		if ((error = cf_db_file_lookup(db, loc.file, &file_name))) {
			goto fail_iter;
		}

		print_one_typename(&entry, &loc, &file_name);

		cf_str_free(&file_name);
	}

fail_iter:
	db_typename_iter_free(&iter);
fail:
	return error;
}

static void
print_type_entry(type_ref_t id, db_type_entry_t *entry,
		loc_ctx_t *loc, const cf_str_t *file_)
{
	const cf_str_t *file;
	cf_str_t default_file;
	if (cf_str_is_null(file_)) {
		cf_str_borrow("<none>", 6, &default_file);
		file = &default_file;
	} else {
		file = file_;
	}
	user_print("%lld %s at %.*s:%u:%u\n",
			p_(id.rowid),
			db_type_kind_str(entry->kind),
			(int)cf_str_len(file),
			file->str,
			loc->line,
			loc->column
			);
}

static void
print_one_typename(db_typename_t *name, loc_ctx_t *loc, const cf_str_t *file)
{
	user_print("%lld '%.*s' at %.*s:%u:%u\n",
			p_(name->base_type.rowid),
			(int)cf_str_len(&name->name),
			name->name.str,
			(int)cf_str_len(file),
			file->str,
			loc->line,
			loc->column
			);
}

static void
print_member_entry(type_ref_t parent, const db_member_t *entry,
		const loc_ctx_t *loc, const cf_str_t *file)
{
	user_print("%lld.'%.*s', type %lld, at %.*s:%u:%u\n",
			p_(parent.rowid),
			(int)cf_str_len(&entry->name),
			entry->name.str,
			p_(entry->base_type.rowid),
			(int)cf_str_len(file),
			file->str,
			loc->line,
			loc->column
			);
}
