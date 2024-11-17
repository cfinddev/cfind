/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "mem_db.h"

#include "cf_assert.h"
#include "cf_print.h"
#include "cf_string.h"
#include "cf_vector.h"

#include <errno.h>
#include <string.h>

/*
 * Indices into `mem_db_t::locs` for each type of entry.
 */
enum {
	type_idx = 0,
	typename_idx = 1,
	member_idx = 2,
	type_use_idx = 3,
};

// codegen macros for `mem_db_t` vectors
CF_VEC_FUNC_DECL(file_vec_t, cf_str_t, file_vec);
CF_VEC_FUNC_DECL(type_vec_t, db_type_entry_t, type_vec);
CF_VEC_FUNC_DECL(typename_vec_t, db_typename_t, typename_vec);
CF_VEC_FUNC_DECL(member_vec_t, db_member_t, member_vec);
CF_VEC_FUNC_DECL(type_use_vec_t, db_type_use_t, type_use_vec);
CF_VEC_FUNC_DECL(loc_vec_t, loc_ctx_t, loc_vec);

CF_VEC_ITER_GENERATE(file_vec_t, cf_str_t, file_iter);
// CF_VEC_ITER_GENERATE(type_vec_t, db_type_entry_t, type_iter);
CF_VEC_ITER_GENERATE(typename_vec_t, db_typename_t, typename_iter);
CF_VEC_ITER_GENERATE(member_vec_t, db_member_t, member_iter);
// CF_VEC_ITER_GENERATE(type_use_vec_t, db_type_use_t, type_use_iter);

static void mem_db_free_files(file_vec_t *vec);
static void mem_db_free_types(type_vec_t *vec);
static void mem_db_free_typenames(typename_vec_t *vec);
static void mem_db_free_members(member_vec_t *vec);
static void mem_db_free_type_uses(type_use_vec_t *vec);
static void mem_db_free_locs(loc_vec_t *vec);

int
mem_db_open(mem_db_t *db)
{
	cf_print_debug("open memdb %p\n", db);
	file_vec_make(&db->files);
	type_vec_make(&db->user_types);
	typename_vec_make(&db->typenames);
	member_vec_make(&db->members);
	type_use_vec_make(&db->type_uses);

	for (unsigned i = 0; i < MEM_DB_NUM_VEC; ++i) {
		loc_vec_make(&db->locs[i]);
	}
	return 0;
}

int
mem_db_close(mem_db_t *db)
{
	cf_print_debug("close memdb %p, %zu files\n",
			db, file_vec_len(&db->files));
	mem_db_free_files(&db->files);
	mem_db_free_types(&db->user_types);
	mem_db_free_typenames(&db->typenames);
	mem_db_free_members(&db->members);
	mem_db_free_type_uses(&db->type_uses);
	mem_db_free_locs(db->locs);

	return 0;
}

/*
 * Add a file to in-memory database `db`.
 *
 * Note: no effort is made to normalize `path`, or to detect whether it's been
 * inserted before.
 *
 * On success, `*out` is set to the (shifted) index of the new file entry.
 * Because `file_ref_t` uses value 0 for invalid values, indices of file
 * entries are 1-based.
 */
int
mem_db_add_file(mem_db_t *db, const char *path, size_t len, size_t *out)
{
	int error;
	cf_str_t *file = file_vec_reserve(&db->files);
	if (!file) {
		error = ENOMEM;
		goto fail;
	}
	if ((error = cf_str_dup(path, len, file))) {
		goto fail_file;
	}

	file_vec_commit(&db->files, file);
	*out = file_vec_len(&db->files);

	return 0;
fail_file:
	file_vec_abort(&db->files, file);
fail:
	return error;
}

/*
 * Check for existence of a type matching `name` in the file specified by
 * `loc`.
 *
 * If it exists, return the entry's rowid via `*out`, if not this
 * function returns ENOENT.
 *
 * Steps:
 * - iterate over `db->typenames`
 *   - strcmp each entry's name with `name`
 *   - if there's a match, check whether `loc` matches `db->locs`
 *   - if not, continue on
 */
int
mem_db_typename_lookup(mem_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *name, size_t *out)
{
	int error = ENOENT;
	cf_vec_iter_t iter;

	typename_iter_make(&db->typenames, &iter);

	if (!typename_vec_len(&db->typenames)) {
		error = ENOENT;
		goto fail;
	}

	// base pointer
	const db_typename_t *base = typename_vec_at(&db->typenames, 0);

	// check each typename entry
	while (typename_iter_next(&iter)) {
		db_typename_t *entry = typename_iter_peek(&iter);
		const size_t len = cf_str_len(&entry->name);

		// check names
		if (cf_str_len(&name->name) != len) {
			continue;
		}
		if (strncmp(name->name.str, entry->name.str, len)) {
			continue;
		}
		// name match, check location

		// compute location index
		cf_assert(entry >= base);
		const size_t i = entry - base;
		const loc_ctx_t *loc2 = loc_vec_at(&db->locs[typename_idx], i);

		// check locations match
		if (loc->file.index != loc2->file.index) {
			continue;
		}

		// a match
		error = 0;
		*out = i;
		break;
	}

fail:
	typename_iter_free(&iter);
	return error;
}

int
mem_db_type_insert(mem_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, size_t *out)
{
	int error;

	// reserve space for `entry`
	db_type_entry_t *new_entry = type_vec_reserve(&db->user_types);
	if (!new_entry) {
		error = ENOMEM;
		goto fail;
	}

	// reserve for `loc`
	loc_ctx_t *new_loc = loc_vec_reserve(&db->locs[type_idx]);
	if (!new_loc) {
		error = ENOMEM;
		goto fail_loc;
	}

	// copy
	memcpy(new_entry, entry, sizeof(*entry));
	memcpy(new_loc, loc, sizeof(*loc));

	// commit
	type_vec_commit(&db->user_types, new_entry);
	loc_vec_commit(&db->locs[type_idx], new_loc);

	// set `out` to index of new type entry
	// this is equal to the type vector's new length
	// note the shift by 1: type at index 0 uses ID 1
	*out = type_vec_len(&db->user_types);

	return 0;
fail_loc:
	type_vec_abort(&db->user_types, new_entry);
fail:
	return error;
}

int
mem_db_typename_insert(mem_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry)
{
	int error;

	// reserve space for `entry`
	db_typename_t *new_entry = typename_vec_reserve(&db->typenames);
	if (!new_entry) {
		error = ENOMEM;
		goto fail;
	}

	// reserve for `loc`
	loc_ctx_t *new_loc = loc_vec_reserve(&db->locs[typename_idx]);
	if (!new_loc) {
		error = ENOMEM;
		goto fail_loc;
	}

	// copy
	// heap allocate `entry`s name
	*new_entry = (db_typename_t) {
		.kind = entry->kind,
		.base_type.index = entry->base_type.index,
	};
	if ((error = cf_str_dup_str(&entry->name, &new_entry->name))) {
		goto fail_copy;
	}
	memcpy(new_loc, loc, sizeof(*loc));

	// commit
	typename_vec_commit(&db->typenames, new_entry);
	loc_vec_commit(&db->locs[typename_idx], new_loc);

	return 0;
fail_copy:
	loc_vec_abort(&db->locs[typename_idx], new_loc);
fail_loc:
	typename_vec_abort(&db->typenames, new_entry);
fail:
	return error;
}

int
mem_db_member_insert(mem_db_t *db, const loc_ctx_t *loc,
		const db_member_t *entry)
{
	int error;

	// reserve space for `entry`
	db_member_t *new_entry = member_vec_reserve(&db->members);
	if (!new_entry) {
		error = ENOMEM;
		goto fail;
	}

	// reserve for `loc`
	loc_ctx_t *new_loc = loc_vec_reserve(&db->locs[member_idx]);
	if (!new_loc) {
		error = ENOMEM;
		goto fail_loc;
	}

	// copy
	// heap allocate `entry`s name
	*new_entry = (db_member_t) {
		.parent.index = entry->parent.index,
		.base_type.index = entry->base_type.index,
	};
	if ((error = cf_str_dup_str(&entry->name, &new_entry->name))) {
		goto fail_copy;
	}
	memcpy(new_loc, loc, sizeof(*loc));

	// commit
	member_vec_commit(&db->members, new_entry);
	loc_vec_commit(&db->locs[member_idx], new_loc);

	return 0;
fail_copy:
	loc_vec_commit(&db->locs[member_idx], new_loc);
fail_loc:
	member_vec_abort(&db->members, new_entry);
fail:
	return error;
}

int
mem_db_type_use_insert(mem_db_t *db, const loc_ctx_t *loc,
		const db_type_use_t *entry)
{
	int error;

	// reserve space for `entry`
	db_type_use_t *new_entry = type_use_vec_reserve(&db->type_uses);
	if (!new_entry) {
		error = ENOMEM;
		goto fail;
	}

	// reserve for `loc`
	loc_ctx_t *new_loc = loc_vec_reserve(&db->locs[type_use_idx]);
	if (!new_loc) {
		error = ENOMEM;
		goto fail_loc;
	}

	// copy
	memcpy(new_entry, entry, sizeof(*entry));
	memcpy(new_loc, loc, sizeof(*loc));

	// commit
	type_use_vec_commit(&db->type_uses, new_entry);
	loc_vec_commit(&db->locs[type_use_idx], new_loc);

	return 0;
fail_loc:
	type_use_vec_abort(&db->type_uses, new_entry);
fail:
	return error;
}

int
mem_db_file_lookup(mem_db_t *db, size_t id, cf_str_t *out)
{
	cf_assert(id);
	const size_t index = id - 1;

	if (index >= file_vec_len(&db->files)) {
		return ENOENT;
	}

	const cf_str_t *file_name = file_vec_at(&db->files, index);
	cf_str_dup_str(file_name, out);
	return 0;
}

int
mem_db_type_lookup(mem_db_t *db, size_t id, db_type_entry_t *entry_out,
		loc_ctx_t *loc_out)
{
	cf_assert(id);
	const size_t index = id - 1;

	if (index >= type_vec_len(&db->user_types)) {
		return ENOENT;
	}

	// find entry and location
	const db_type_entry_t *entry = type_vec_at(&db->user_types, index);
	const loc_ctx_t *loc = loc_vec_at(&db->locs[type_idx], index);

	// copy out
	memcpy(entry_out, entry, sizeof(*entry));
	memcpy(loc_out, loc, sizeof(*loc));
	return 0;
}

/*
 * Search member entries for `parent`,`name`.
 *
 * Similar to mem_db_typename_lookup().
 */
int
mem_db_member_lookup(mem_db_t *db, size_t parent, const cf_str_t *name,
		db_member_t *entry_out, loc_ctx_t *loc_out)
{
	int error = ENOENT;

	cf_vec_iter_t iter;
	member_iter_make(&db->members, &iter);

	if (!member_vec_len(&db->members)) {
		error = ENOENT;
		goto fail;
	}

	// base pointer
	const db_member_t *base = member_vec_at(&db->members, 0);

	// check each member entry
	while (member_iter_next(&iter)) {
		db_member_t *entry = member_iter_peek(&iter);

		// check parents
		if (parent != entry->parent.index) {
			continue;
		}

		// check names
		const size_t len = cf_str_len(&entry->name);
		if (cf_str_len(name) != len) {
			continue;
		}
		if (strncmp(name->str, entry->name.str, len)) {
			continue;
		}

		// a match

		// compute location index
		cf_assert(entry >= base);
		const size_t i = entry - base;
		const loc_ctx_t *loc = loc_vec_at(&db->locs[member_idx], i);

		// copy out
		*entry_out = (db_member_t) {
			.parent.index = entry->parent.index,
			.base_type.index = entry->base_type.index,
		};
		if ((error = cf_str_dup_str(&entry->name, &entry_out->name))) {
			goto fail;
		}
		memcpy(loc_out, loc, sizeof(*loc));

		break;
	}

fail:
	typename_iter_free(&iter);
	return error;
}

/*
 * Create an iterator over typename entries in search of `name`.
 */
int
mem_db_typename_find(mem_db_t *db, const cf_str_t *name,
		mem_db_typename_iter_t *out)
{
	(void)db;
	// initialize to 0xffff...
	// the first _next() call will increment, then check against length
	memset(out, 0, sizeof(*out));
	out->i = SIZE_MAX;
	cf_str_borrow_str(name, &out->key);
	return 0;
}

void
mem_db_typename_iter_free(mem_db_typename_iter_t *it)
{
	// a nop
	cf_str_free(&it->key);
}

void
mem_db_typename_iter_peek(const mem_db_t *db, const mem_db_typename_iter_t *it,
		db_typename_t *entry_out, loc_ctx_t *loc_out)
{
	const size_t i = it->i;
	const db_typename_t *entry = typename_vec_at(&db->typenames, i);

	// copy most of typename entry, but borrow name string
	memcpy(entry_out, entry, sizeof(*entry_out));
	cf_str_borrow_str(&entry->name, &entry_out->name);

	memcpy(loc_out, loc_vec_at(&db->locs[typename_idx], i), sizeof(*loc_out));
}

/*
 * Iterate in [i+i, len) searching for the first next entry matching it->key
 *
 * On success, `it->i` is left equal to the index of the next matching entry.
 *
 * Note: `i` overflows from SIZE_MAX to 0 on first call.
 */
bool
mem_db_typename_iter_next(mem_db_t *db, mem_db_typename_iter_t *it)
{
	const size_t vec_len = typename_vec_len(&db->typenames);
	const size_t name_len = cf_str_len(&it->key);

	for (size_t i = it->i + 1; i < vec_len; ++i) {
		const db_typename_t *entry = typename_vec_at(&db->typenames, i);
		// check names
		if (cf_str_len(&entry->name) != name_len) {
			continue;
		}
		if (strncmp(it->key.str, entry->name.str, name_len)) {
			continue;
		}

		// a match
		it->i = i;
		return true;
	}

	// no more matches
	return false;
}

static void
mem_db_free_files(file_vec_t *vec)
{
	// iter over `db->files`
	// free each file string
	cf_vec_iter_t file_iter;
	file_iter_make(vec, &file_iter);
	while (file_iter_next(&file_iter)) {
		cf_str_t *str = file_iter_peek(&file_iter);
		cf_print_debug("remove file str %p\n", str->str);
		cf_str_free(str);
	}
	file_iter_free(&file_iter);
	file_vec_free(vec);
}

static void
mem_db_free_types(type_vec_t *vec)
{
	// no external resources
	// just free the vector
	type_vec_free(vec);
}

static void
mem_db_free_typenames(typename_vec_t *vec)
{
	// iter over typenames
	// free each name string
	cf_vec_iter_t typename_iter;
	typename_iter_make(vec, &typename_iter);
	while (typename_iter_next(&typename_iter)) {
		db_typename_t *entry = typename_iter_peek(&typename_iter);
		cf_print_debug("remove typename str %p, '%.*s'\n",
				entry, (int)cf_str_len(&entry->name), entry->name.str);
		cf_str_free(&entry->name);
	}
	typename_iter_free(&typename_iter);
	typename_vec_free(vec);
}

static void
mem_db_free_members(member_vec_t *vec)
{
	// iter over members
	// free each name string
	cf_vec_iter_t member_iter;
	member_iter_make(vec, &member_iter);
	while (member_iter_next(&member_iter)) {
		db_member_t *entry = member_iter_peek(&member_iter);
		cf_str_free(&entry->name);
	}
	member_iter_free(&member_iter);
	member_vec_free(vec);
}

static void
mem_db_free_type_uses(type_use_vec_t *vec)
{
	type_use_vec_free(vec);
}

static void
mem_db_free_locs(loc_vec_t *vec)
{
	// no external resources
	// just free each vector
	for (unsigned i = 0; i < MEM_DB_NUM_VEC; ++i) {
		loc_vec_free(&vec[i]);
	}
}
