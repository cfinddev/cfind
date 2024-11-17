/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Nop database backend.
 */
#pragma once

#include "cc_support.h"
#include "db_types.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

__BEGIN_DECLS

/*
 * Nop database implementation.
 *
 * This satisfies the interface of `cf_db_t` but doesn't actually store
 * anything.
 */
typedef struct {
	int64_t file_id;
	int64_t type_id;
} nop_db_t;

typedef struct {
	char unused;
} nop_db_typename_iter_t;

int nop_db_open(nop_db_t *out);

int nop_db_add_file(nop_db_t *db, const char *path, size_t len, int64_t *out);
int nop_db_add_type(nop_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, const db_typename_t *name,
		int64_t *out);

int nop_db_typename_lookup(nop_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *name, int64_t *out);
int nop_db_type_insert(nop_db_t *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, int64_t *out);
int nop_db_typename_insert(nop_db_t *db, const loc_ctx_t *loc,
		const db_typename_t *entry);
int nop_db_member_insert(nop_db_t *db, const loc_ctx_t *loc,
		const db_member_t *entry);
int nop_db_type_use_insert(nop_db_t *db, const loc_ctx_t *loc,
		const db_type_use_t *entry);

int nop_db_type_lookup(nop_db_t *db, int64_t id,
		db_type_entry_t *entry_out, loc_ctx_t *loc_out);
int nop_db_file_lookup(nop_db_t *db, int64_t id, cf_str_t *out);
int nop_db_member_lookup(nop_db_t *db, int64_t parent, const cf_str_t *member,
		db_member_t *entry_out, loc_ctx_t *loc_out);
int nop_db_typename_find(nop_db_t *db, const cf_str_t *name,
		nop_db_typename_iter_t *out);

void nop_db_typename_iter_free(nop_db_typename_iter_t *it);
void nop_db_typename_iter_peek(const nop_db_t *db,
		const nop_db_typename_iter_t *it, db_typename_t *entry_out,
		loc_ctx_t *loc_out);
bool nop_db_typename_iter_next(nop_db_t *db, nop_db_typename_iter_t *it);

__END_DECLS
