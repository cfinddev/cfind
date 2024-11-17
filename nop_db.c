/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "nop_db.h"

#include "cf_assert.h"

#include <errno.h>
#include <string.h>

/*
 * NOP db implementation.
 */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

int
nop_db_open(nop_db_t *out)
{
	memset(out, 0, sizeof(*out));
	return 0;
}

int
nop_db_add_file(nop_db_t *db, const char *path, size_t len, int64_t *out)
{
	++db->file_id;
	*out = db->file_id;
	return 0;
}

int
nop_db_add_type(nop_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, const db_typename_t *name, int64_t *out)
{
	++db->type_id;
	*out = db->type_id;
	return 0;
}

int
nop_db_typename_lookup(nop_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *name, int64_t *out)
{
	return ENOENT;
}

int
nop_db_type_insert(nop_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, int64_t *out)
{
	++db->type_id;
	*out = db->type_id;
	return 0;
}

int
nop_db_typename_insert(nop_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry)
{
	return 0;
}

int
nop_db_member_insert(nop_db_t *db, const loc_ctx_t *loc,
		const db_member_t *entry)
{
	return 0;
}

int
nop_db_type_use_insert(nop_db_t *db, const loc_ctx_t *loc,
		const db_type_use_t *entry)
{
	return 0;
}

int
nop_db_type_lookup(nop_db_t *db, int64_t id, db_type_entry_t *entry_out,
		loc_ctx_t *loc_out)
{
	return ENOENT;
}

int
nop_db_file_lookup(nop_db_t *db, int64_t id, cf_str_t *out)
{
	return ENOENT;
}

int
nop_db_typename_find(nop_db_t *db, const cf_str_t *name,
		nop_db_typename_iter_t *out)
{
	return ENOTSUP;
}

int
nop_db_member_lookup(nop_db_t *db, int64_t parent, const cf_str_t *member,
		db_member_t *entry_out, loc_ctx_t *loc_out)
{
	return ENOENT;
}

void
nop_db_typename_iter_free(nop_db_typename_iter_t *it)
{
	cf_panic("nop typename iterator not supported");
}

void
nop_db_typename_iter_peek(const nop_db_t *db, const nop_db_typename_iter_t *it,
		db_typename_t *entry_out, loc_ctx_t *loc_out)
{
	cf_panic("nop typename iterator not supported");
}

bool
nop_db_typename_iter_next(nop_db_t *db, nop_db_typename_iter_t *it)
{
	cf_panic("nop typename iterator not supported");
}

#pragma clang diagnostic pop // -Wunused-parameter
