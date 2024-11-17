/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "test_utils.h"
#include "marker.h"
#include "src_adaptor.h"
#include "../cf_string.h"
#include "../cf_index.h"
#include "../cf_db.h"
#include "../db_types.h"

#include <string.h>
#include <errno.h>

static int test_basic_struct(void);
TEST_DECL(test_basic_struct);

static int
index_wrapper(src_adaptor_t *adp, cf_db_t *db)
{
	const index_config_t config = {
		.db_kind = index_db_borrowed,
		.input_kind = input_source_file,
		.db_args.db = db,
		.input_path = adp->path,
	};

	return cf_index_project(&config);
}

/*
 * All on-disk state created for a single type.
 */
typedef struct {
	db_typename_t name;
	loc_ctx_t name_loc;
	db_type_entry_t type;
	loc_ctx_t type_loc;
} full_type_t;

/*
 * Do a lookup of for a type identified by `name`.
 *
 * There should only be exactly one entry for `name`.
 * Return all entries -- typename, type entry, locations -- via `out`.
 */
static int
lookup_one_type(cf_db_t *db, const cf_str_t *name, full_type_t *out)
{
	int error;

	// look up, make iterator
	db_typename_iter_t iter;
	if ((error = cf_db_typename_find(db, name, &iter))) {
		goto fail;
	}

	// get first entry
	if (!db_typename_iter_next(&iter)) {
		error = ENOENT;
		goto fail_iter;
	}

	// copy name to output
	db_typename_iter_peek(&iter, &out->name, &out->name_loc);
	// convert borrowed to owned
	cf_str_promote(&out->name.name);

	// check there's no more entries
	if (db_typename_iter_next(&iter)) {
		// too many
		error = EMLINK;
		goto fail_iter;
	}

	// resolve name to type
	if ((error = cf_db_type_lookup(db, out->name.base_type,
			&out->type, &out->type_loc))) {
		goto fail_iter;
	}

fail_iter:
	db_typename_iter_free(&iter);
fail:
	return error;
}

/*
 * Test that a single `struct` definition is indexed.
 *
 * Steps:
 * - parse source markers from a C snippet
 * - create an in-memory database
 * - run the indexer
 * - check the database has an entry matching the C snippet
 *   check locations match the source marker
 */
static int
test_basic_struct(void)
{
	static const char *const src = "/*@@>0*/struct foo { int a; };\n";

	// parse marker 0 into line/column
	source_marker_t markers;
	ASSERT_EQ(find_markers(src, strlen(src), &markers), 0);
	ASSERT_EQ(markers.n, 1);
	const source_line_t *marker = &markers.markers[0];

	src_adaptor_t adp;
	ASSERT_EQ(make_src_adaptor(src, strlen(src), &adp), 0);

	cf_db_t db;
	ASSERT_EQ(cf_db_open_mem(&db), 0);

	// do real indexing
	ASSERT_EQ(index_wrapper(&adp, &db), 0);

	// look up struct entry
	full_type_t entry;
	cf_str_t struct_name;
	cf_str_borrow("foo", 3, &struct_name);
	ASSERT_EQ(lookup_one_type(&db, &struct_name, &entry), 0);

	// check values

	// name entry
	ASSERT_EQ(entry.name.kind, name_kind_direct);
	ASSERT_NEQ(entry.name.base_type.index, 0);
	ASSERT_EQ(cf_str_len(&entry.name.name), 3);
	ASSERT_EQ(memcmp(entry.name.name.str, struct_name.str, 3), 0);

	ASSERT_EQ(entry.name_loc.line, marker->line);
	ASSERT_EQ(entry.name_loc.column, marker->column);

	// type entry
	ASSERT_EQ(entry.type.kind, type_kind_struct);
	ASSERT(entry.type.complete);

	ASSERT_EQ(entry.type_loc.line, marker->line);
	ASSERT_EQ(entry.type_loc.column, marker->column);

	cf_str_free(&entry.name.name);
	cf_db_close(&db);
	free_src_adaptor(&adp);
	return 0;
}
