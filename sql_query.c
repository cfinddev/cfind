/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "sql_query.h"

#include "cc_support.h"
#include "cf_assert.h"
#include "cf_print.h"
#include "cf_string.h"
#include "sql_schema.h"
#include "sql_types.h"
#include "query_desc.h"

#include <sqlite3.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

static int config_db(sqlite3 *db);
static int create_tables(sqlite3 *db);

// query compilation functions
static sqlite3_stmt *compile_file_table_create(sqlite3 *db);
static sqlite3_stmt *compile_type_table_create(sqlite3 *db);
static sqlite3_stmt *compile_typename_table_create(sqlite3 *db);
static sqlite3_stmt *compile_incomplete_type_table_create(sqlite3 *db);
static sqlite3_stmt *compile_type_use_table_create(sqlite3 *db);
static sqlite3_stmt *compile_member_table_create(sqlite3 *db);

static sqlite3_stmt *compile_file_table_lookup(sqlite3 *db);
static sqlite3_stmt *compile_file_table_id_lookup(sqlite3 *db);
static sqlite3_stmt *compile_file_table_insert(sqlite3 *db);
static sqlite3_stmt *compile_type_table_lookup(sqlite3 *db);
static sqlite3_stmt *compile_type_table_insert(sqlite3 *db);
static sqlite3_stmt *compile_typename_table_lookup(sqlite3 *db);
static sqlite3_stmt *compile_typename_table_find(sqlite3 *db);
static sqlite3_stmt *compile_typename_table_insert(sqlite3 *db);
static sqlite3_stmt *compile_incomplete_type_table_lookup(sqlite3 *db);
static sqlite3_stmt *compile_type_use_table_insert(sqlite3 *db);
static sqlite3_stmt *compile_member_table_insert(sqlite3 *db);
static sqlite3_stmt *compile_member_table_lookup(sqlite3 *db);

static sqlite3_stmt *compile_query_desc(
		sqlite3 *db, const query_desc_t *query);
static sqlite3_stmt *compile_query_(
		sqlite3 *db, const char *query, size_t len) __attribute__((noinline));

// bind functions
static int bind_file_lookup(
		sqlite3_stmt *stmt, const char *path, size_t len);
static int bind_file_id_lookup(sqlite3_stmt *stmt, int64_t rowid);
static int bind_file_insert(
		sqlite3_stmt *stmt, const char *path, size_t len);
static int bind_type_lookup(sqlite3_stmt *stmt, int64_t rowid);
static int bind_type_insert(
		sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_type_entry_t *entry);
static int bind_typename_lookup(
		sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_typename_t *name);
static int bind_typename_find(sqlite3_stmt *stmt, const cf_str_t *name);
static int bind_typename_insert(
		sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_typename_t *name);
static int bind_type_use_insert(
		sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_type_use_t *entry);
static int bind_member_insert(
		sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_member_t *entry);
static int bind_member_lookup(
		sqlite3_stmt *stmt, int64_t parent, const cf_str_t *name);

// lookup query execute functions
static int exec_lookup_file_query(sqlite3_stmt *stmt, int64_t *rowid_out);
static int exec_file_id_lookup_query(sqlite3_stmt *stmt, cf_str_t *path_out);
static int exec_lookup_typename_query(sqlite3_stmt *stmt, int64_t *rowid_out,
		typename_kind_t *kind_out);
static int exec_find_typename_query(sqlite3_stmt *stmt,
		db_typename_t *entry_out, loc_ctx_t *loc_out);
static int exec_lookup_type(sqlite3_stmt *stmt, int64_t *rowid_out,
		db_type_entry_t *entry_out, loc_ctx_t *loc_out);
static int exec_lookup_member(sqlite3_stmt *stmt,
		db_member_t *entry_out, loc_ctx_t *loc_out);

static int lookup_one_row(sqlite3_stmt *stmt, const lookup_desc_t *desc,
		column_val_t *out);
static int query_step_one(sqlite3_stmt *stmt);

// generic `serial_row_t` functions
static int bind_serial_row(sqlite3_stmt *stmt, const serial_row_t *row);
static int select_serial_row(sqlite3_stmt *stmt, const serial_row_t *out);

static int bind_one_column(sqlite3_stmt *stmt, int bind_index,
		column_kind_t column_kind, const column_val_t *val);
static int select_one_column(sqlite3_stmt *stmt, int index,
		column_kind_t expected_kind, column_val_t *out);

static int sql_column_kind2type(column_kind_t kind);

/*
 * Macro wrapper to compile_query_().
 *
 * This mainly exists to assert `query` is a string literal. This makes writing
 * sql injection-vulnerable code harder. The set of query strings is fixed at
 * build time (they can only come from the C string section).
 *
 * sql states
 * > passing an nByte parameter that
 * > is the number of bytes in the input string <i>including</i>
 * > the nul-terminator.
 *
 * Pass in the `sizeof`, rather than the strlen(3), of `query`.
 */
#define compile_query(db, query) ({ \
	_Static_assert(__builtin_constant_p(query), \
			"compile_query() only accepts string literal queries"); \
	compile_query_(db, query, sizeof(query)); \
})

/*
 * Open a sqlite database at path `db_path`.
 *
 * On success, the database handle is written to `*sql_out`.
 *
 * Steps:
 * - initialize sqlite3
 *   This only does anything once per process.
 * - open the db
 * - configure db
 * - create tables
 *   - file table
 *   - type table
 *   ...
 * - in read/write mode, enter a transaction for all future inserts
 *
 * XXX `ro=true` is broken; sqlite3_open fails with 21 SQLITE_MISUSE ???
 */
int
sql_open(const char *db_path, bool ro, sqlite3 **sql_out)
{
	const int flags = SQLITE_OPEN_CREATE | SQLITE_OPEN_PRIVATECACHE |
			(ro ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE);
	int error;
	sqlite3 *db = NULL;

	// initialize sqlite library
	if ((error = sqlite3_initialize())) {
		cf_print_err("cannot init sqlite: %d, '%s'\n",
				error, sqlite3_errstr(error));
		return error;
	}

	// get a db handle
	if ((error = sqlite3_open_v2(db_path, &db, flags, NULL))) {
		cf_print_debug("sqlite3_open() failed with %d, '%s'\n",
				error, sqlite3_errstr(error));
		return error;
	}

	// do top-level configuration
	if ((error = config_db(db))) {
		goto fail;
	}

	// skip everything else when opening the db in readonly mode
	if (ro) {
		cf_print_info("readonly db; skipping table creation\n");
		goto done;
	}

	// create every table in the database
	if ((error = create_tables(db))) {
		goto fail;
	}

done:
	*sql_out = db;
	return 0;
fail:
	sqlite3_close(db);
	return error;
}

/*
 * Do top-level db configuration.
 *
 * Steps:
 * - turn on WAL mode
 * - make WAL file persistent
 */
static int
config_db(sqlite3 *db)
{
	int error;
	char *errmsg;
	int persist_wal = 1;

	// XXX consider _prepare() and _step() instead
	if ((error = sqlite3_exec(db,
			"PRAGMA journal_mode=WAL;", NULL, NULL, &errmsg)))  {
		cf_print_err("cannot turn on WAL mode: '%s'\n", errmsg);
		sqlite3_free(errmsg);
		goto fail;
	}

	if ((error = sqlite3_file_control(db, NULL, SQLITE_FCNTL_PERSIST_WAL,
			&persist_wal))) {
		cf_print_err("cannot turn on WAL persistence: %d, '%s'\n",
				error, sqlite3_errstr(error));
		goto fail;
	}

fail:
	return error;
}

/*
 * For a read/write database, create all cf tables in `db`.
 *
 * Only tables that don't already exist are created. Preexisting tables are
 * left alone.
 */
static int
create_tables(sqlite3 *db)
{
#define CF_NUM_TABLES 6
	int error;

	static const char *const table_names[] = {
		FILE_TABLE_NAME,
		TYPE_TABLE_NAME,
		TYPENAME_TABLE_NAME,
		INCOMPLETE_TYPE_TABLE_NAME,
		TYPE_USE_TABLE_NAME,
		MEMBER_TABLE_NAME,
	};

	// an array of sql CREATE statements
	sqlite3_stmt *const create_stmts[] = {
		compile_file_table_create(db),
		compile_type_table_create(db),
		compile_typename_table_create(db),
		compile_incomplete_type_table_create(db),
		compile_type_use_table_create(db),
		compile_member_table_create(db),
	};

	_Static_assert(ARRAY_LEN(table_names) == CF_NUM_TABLES,
			"keep array sizes synced");
	_Static_assert(ARRAY_LEN(create_stmts) == CF_NUM_TABLES,
			"keep array sizes synced");

	// execute each table CREATE statement
	for (unsigned i = 0; i < CF_NUM_TABLES; ++i) {
		if ((error = sqlite3_step(create_stmts[i])) != SQLITE_DONE) {
			cf_print_err("cannot create table '%s', error %d/'%s'\n",
					table_names[i], error, sqlite3_errmsg(db));
			// Note: already created tables aren't removed
			break;
		}
	}

	// free all statements
	for (unsigned i = 0; i < CF_NUM_TABLES; ++i) {
		sqlite3_finalize(create_stmts[i]);
	}

	// map `SQLITE_DONE` to success
	error = (error == SQLITE_DONE) ? 0 : error;
	return error;
}

/*
 * Do a lookup for a file whose name exactly matches `path`.
 *
 * The purpose of this function is to test for existence. The only data
 * returned from the query is the rowid; it's written to `*rowid_out`.
 */
int
lookup_file(sqlite3 *db, const char *path, size_t len, int64_t *rowid_out)
{
	cf_assert(len);

	int error;
	sqlite3_stmt *stmt = compile_file_table_lookup(db);

	if ((error = bind_file_lookup(stmt, path, len))) {
		goto fail;
	}

	if ((error = exec_lookup_file_query(stmt, rowid_out))) {
		cf_print_err("failed execute file-lookup: %d\n", error);
		goto fail;
	}

fail:
	sqlite3_finalize(stmt);
	return error;
}

/*
 * Do a lookup for a file with id equal to `rowid`.
 *
 * The only data returned from the query is the name. `out` is set to an
 * *owned* string containing the file's path.
 */
int
lookup_file_id(sqlite3 *db, int64_t rowid, cf_str_t *out)
{
	int error;
	sqlite3_stmt *stmt = compile_file_table_id_lookup(db);

	if ((error = bind_file_id_lookup(stmt, rowid))) {
		goto fail;
	}

	if ((error = exec_file_id_lookup_query(stmt, out))) {
		cf_print_err("failed execute file-lookup: %d\n", error);
		goto fail;
	}

	cf_print_info("lookup-file -> %p,%zu '%.*s'\n",
			out->str, cf_str_len(out),
			(int)cf_str_len(out),
			out->str
			);

fail:
	sqlite3_finalize(stmt);
	return error;

}

/*
 * Insert a path into the file table.
 *
 * The new rowid is assigned to `*rowid_out`.
 */
int
insert_file(sqlite3 *db, const char *path, size_t len, int64_t *rowid_out)
{
	cf_assert(len);

	int error;
	sqlite3_stmt *stmt = compile_file_table_insert(db);

	// serialize `path` to `stmt`
	if ((error = bind_file_insert(stmt, path, len))) {
		goto fail;
	}

	// execute query
	error = sqlite3_step(stmt);
	if (error != SQLITE_DONE) {
		cf_print_err("insert-file query execute failed, error %d\n", error);
		goto fail;
	}
	error = 0;

	// get back the rowid of the just-inserted row
	const int64_t rowid = sqlite3_last_insert_rowid(db);
	cf_assert(rowid > 0);
	*rowid_out = rowid;

fail:
	sqlite3_finalize(stmt);
	return error;
}

/*
 * Insert `entry` into the type table.
 *
 * On success, the new rowid is returned via `*rowid_out`.
 *
 * This function only inserts into the type table. It's the caller's job to
 * follow with a separate insertion into the typename table (or wherever) that
 * references `*rowid_out`.
 */
int
insert_complete_type(sqlite3 *db, const loc_ctx_t *loc,
		const db_type_entry_t *entry, int64_t *rowid_out)
{
	int error;
	sqlite3_stmt *stmt = compile_type_table_insert(db);

	// serialize `entry`
	if ((error = bind_type_insert(stmt, loc, entry))) {
		goto fail;
	}

	// execute query
	error = sqlite3_step(stmt);
	if (error != SQLITE_DONE) {
		cf_print_err("insert-type query execute failed, error %d\n", error);
		goto fail;
	}
	error = 0;

	// get back the rowid of the just-inserted row
	const int64_t rowid = sqlite3_last_insert_rowid(db);
	cf_assert(rowid > 0);
	*rowid_out = rowid;

fail:
	sqlite3_finalize(stmt);
	return error;
}

/*
 */
int
insert_typename(sqlite3 *db, const loc_ctx_t *loc, const db_typename_t *name,
		int64_t *rowid_out)
{
	int error;
	sqlite3_stmt *stmt = compile_typename_table_insert(db);

	// serialize `name`
	if ((error = bind_typename_insert(stmt, loc, name))) {
		goto fail;
	}

	// execute query
	error = sqlite3_step(stmt);
	if (error != SQLITE_DONE) {
		cf_print_err("insert-typename query execute failed, error %d\n",
				error);
		goto fail;
	}
	error = 0;

	// get back the rowid of the just-inserted row
	const int64_t rowid = sqlite3_last_insert_rowid(db);
	cf_assert(rowid > 0);
	*rowid_out = rowid;
fail:
	return error;
}

/*
 * Check for existence of a type matching `name` in the file specified by
 * `loc`.
 *
 * If it exists, return the entry's rowid via `*rowid_out`, if not this
 * function returns ENOENT.
 */
int
lookup_typename(sqlite3 *db, const loc_ctx_t *loc, const db_typename_t *name,
		int64_t *rowid_out)
{
	int error;
	sqlite3_stmt *stmt = compile_typename_table_lookup(db);

	if ((error = bind_typename_lookup(stmt, loc, name))) {
		goto fail;
	}

	typename_kind_t found_kind;
	if ((error = exec_lookup_typename_query(stmt, rowid_out, &found_kind))) {
		goto fail;
	}

	// the tag namespace is not shared with the typedef namespace
	// e.g., `struct foo;` is different from `typedef struct {} foo;`
	if (found_kind != name->kind) {
		cf_print_debug("lookup-typename found matching row with wrong kind; "
				"found %u, expected %u\n", found_kind, name->kind);
		error = ENOENT;
	}

fail:
	sqlite3_finalize(stmt);
	return error;
}

int
insert_type_use(sqlite3 *db, const loc_ctx_t *loc, const db_type_use_t *entry,
		int64_t *rowid_out)
{
	int error;
	sqlite3_stmt *stmt = compile_type_use_table_insert(db);

	// serialize `entry`
	if ((error = bind_type_use_insert(stmt, loc, entry))) {
		goto fail;
	}

	// execute query
	error = sqlite3_step(stmt);
	if (error != SQLITE_DONE) {
		cf_print_err("insert-type-use query execute failed, error %d\n",
				error);
		goto fail;
	}
	error = 0;

	// get back the rowid of the just-inserted row
	const int64_t rowid = sqlite3_last_insert_rowid(db);
	cf_assert(rowid > 0);
	*rowid_out = rowid;

fail:
	sqlite3_finalize(stmt);
	return error;
}

int
insert_member(sqlite3 *db, const loc_ctx_t *loc, const db_member_t *entry,
		int64_t *rowid_out)
{
	int error;
	sqlite3_stmt *stmt = compile_member_table_insert(db);

	// serialize `entry`
	if ((error = bind_member_insert(stmt, loc, entry))) {
		goto fail;
	}

	// execute query
	error = sqlite3_step(stmt);
	if (error != SQLITE_DONE) {
		cf_print_err("insert-member query execute failed, error %d\n",
				error);
		goto fail;
	}
	error = 0;

	// get back the rowid of the just-inserted row
	const int64_t rowid = sqlite3_last_insert_rowid(db);
	cf_assert(rowid > 0);
	*rowid_out = rowid;

fail:
	sqlite3_finalize(stmt);
	return error;
}

int
lookup_type_entry(sqlite3 *db, int64_t rowid, db_type_entry_t *entry_out,
		loc_ctx_t *loc_out)
{
	int error;
	sqlite3_stmt *stmt = compile_type_table_lookup(db);

	if ((error = bind_type_lookup(stmt, rowid))) {
		goto fail;
	}

	// execute query
	int64_t rowid_out;
	if ((error = exec_lookup_type(stmt, &rowid_out, entry_out, loc_out))) {
		goto fail;
	}

	cf_assert(rowid_out == rowid);

fail:
	sqlite3_finalize(stmt);
	return error;
}

int
lookup_member(sqlite3 *db, int64_t parent, const cf_str_t *member,
		db_member_t *entry_out, loc_ctx_t *loc_out)
{
	int error;
	sqlite3_stmt *stmt = compile_member_table_lookup(db);

	if ((error = bind_member_lookup(stmt, parent, member))) {
		goto fail;
	}

	// execute query
	if ((error = exec_lookup_member(stmt, entry_out, loc_out))) {
		goto fail;
	}

fail:
	sqlite3_finalize(stmt);
	return error;
}

/*
 * Create a statement that yields all `db_typename_t`s matching `name`.
 *
 * This function does:
 *   compile
 *   bind
 * next():
 *   sql_step
 * get():
 *   deserialize
 */
int
find_typenames(sqlite3 *db, const cf_str_t *name, sqlite3_stmt **out)
{
	int error;
	sqlite3_stmt *stmt = compile_typename_table_find(db);

	if ((error = bind_typename_find(stmt, name))) {
		goto fail;
	}

	*out = stmt;

	return 0;
fail:
	sqlite3_finalize(stmt);
	return error;
}

/*
 * Note: invalidates any borrowed strings returned from a previous
 * iter_get_typename() call.
 */
int
iter_next_typename(sqlite3_stmt *stmt)
{
	return query_step_one(stmt);
}

int
iter_get_typename(sqlite3_stmt *stmt, db_typename_t *entry_out,
		loc_ctx_t *loc_out)
{
	return exec_find_typename_query(stmt, entry_out, loc_out);
}

void
free_typenames(sqlite3_stmt *stmt)
{
	sqlite3_finalize(stmt);
}

/*
 * Execute a file lookup query that has been previously prepared in `stmt`.
 *
 * On success, the rowid of the single matching entry is written to
 * `*rowid_out`.
 */
static int
exec_lookup_file_query(sqlite3_stmt *stmt, int64_t *rowid_out)
{
	int error;

	const size_t num_outputs = file_lookup_query.num_outputs;
	column_val_t column_vals[num_outputs];

	if ((error = lookup_one_row(stmt, &file_lookup_query, column_vals))) {
		goto fail;
	}

	*rowid_out = (int64_t)column_vals[0].uint64_val;

fail:
	return error;
}

/*
 * Do a lookup in the file table according to `stmt`.
 *
 * On success, `path_out` is set to an *owned* string of the file path.
 */
static int
exec_file_id_lookup_query(sqlite3_stmt *stmt, cf_str_t *path_out)
{
	int error;

	const size_t num_outputs = file_id_lookup_query.num_outputs;
	column_val_t column_vals[num_outputs];

	if ((error = lookup_one_row(stmt, &file_id_lookup_query, column_vals))) {
		goto fail;
	}

	cf_str_dup_str(&column_vals[0].str_val, path_out);

fail:
	return error;

}

/*
 * Do a lookup (select one row) in the typename table.
 */
static int
exec_lookup_typename_query(sqlite3_stmt *stmt, int64_t *rowid_out,
		typename_kind_t *kind_out)
{
	int error;

	const size_t num_outputs = typename_lookup_query.num_outputs;
	column_val_t column_vals[num_outputs];

	if ((error = lookup_one_row(stmt, &typename_lookup_query, column_vals))) {
		goto fail;
	}

	*rowid_out = column_vals[0].uint64_val;
	*kind_out = column_vals[1].uint32_val;

fail:
	return error;
}

/*
 * Do a find (select many rows) in the typename table.
 */
static int
exec_find_typename_query(sqlite3_stmt *stmt, db_typename_t *entry_out,
		loc_ctx_t *loc_out)
{
	int error;

	const size_t num_outputs = typename_find_query.num_outputs;
	column_val_t column_vals[num_outputs];

	const serial_row_t srow = {
		.num_columns = typename_find_query.num_outputs,
		.column_kinds = typename_find_query.output_kinds,
		.column_values = column_vals,
	};

	if ((error = select_serial_row(stmt, &srow))) {
		goto fail;
	}

	// note: schema differs from struct member order
	memset(entry_out, 0, sizeof(*entry_out));
	cf_str_borrow_str(&column_vals[0].str_val, &entry_out->name);
	entry_out->kind = column_vals[1].uint32_val;
	entry_out->base_type.rowid = column_vals[2].uint64_val;

	*loc_out = (loc_ctx_t) {
		.file = {
			.rowid = column_vals[3].uint64_val,
		},
		.func = {
			.rowid = column_vals[4].uint64_val,
		},
		.scope = column_vals[5].uint32_val,
		.line = column_vals[6].uint32_val,
		.column = column_vals[7].uint32_val,
	};

	// strings should be borrowed from `stmt`
	cf_assert(column_vals[0].str_val.len & CF_STR_BORROWED);

fail:
	return error;
}

static int
exec_lookup_type(sqlite3_stmt *stmt, int64_t *rowid_out,
		db_type_entry_t *entry_out, loc_ctx_t *loc_out)
{
	int error;

	const size_t num_outputs = type_lookup_query.num_outputs;
	column_val_t column_vals[num_outputs];

	if ((error = lookup_one_row(stmt, &type_lookup_query, column_vals))) {
		goto fail;
	}

	// deserialize from `column_vals` into output parameters

	*rowid_out = column_vals[0].uint64_val;

	*entry_out = (db_type_entry_t) {
		.kind = column_vals[1].uint32_val,
		.complete = column_vals[2].uint32_val,
	};

	*loc_out = (loc_ctx_t) {
		.file = {
			.rowid = column_vals[3].uint64_val,
		},
		.func = {
			.rowid = column_vals[4].uint64_val,
		},
		.scope = column_vals[5].uint32_val,
		.line = column_vals[6].uint32_val,
		.column = column_vals[7].uint32_val,
	};

fail:
	return error;
}

/*
 */
static int
exec_lookup_member(sqlite3_stmt *stmt, db_member_t *entry_out,
		loc_ctx_t *loc_out)
{
	int error;

	const size_t num_outputs = member_lookup_query.num_outputs;
	column_val_t column_vals[num_outputs];

	if ((error = lookup_one_row(stmt, &member_lookup_query, column_vals))) {
		goto fail;
	}

	// deserialize from `column_vals` into output parameters

	*entry_out = (db_member_t) {
		.parent = {
			.rowid = column_vals[0].uint64_val
		},
		.base_type = {
			.rowid = column_vals[1].uint64_val
		},
	};
	cf_str_dup_str(&column_vals[2].str_val, &entry_out->name);

	*loc_out = (loc_ctx_t) {
		.file = {
			.rowid = column_vals[3].uint64_val,
		},
		.func = {
			.rowid = 0,
		},
		.scope = 0,
		.line = column_vals[4].uint32_val,
		.column = column_vals[5].uint32_val,
	};

fail:
	return error;
}

/*
 * For `stmt` as an unexecuted select statement, look up exactly one row and
 * return its columns via `out`.
 */
static int
lookup_one_row(sqlite3_stmt *stmt, const lookup_desc_t *desc,
		column_val_t *out)
{
	int error;

	if ((error = query_step_one(stmt))) {
		goto fail;
	}

	const serial_row_t srow = {
		.num_columns = desc->num_outputs,
		.column_kinds = desc->output_kinds,
		.column_values = out,
	};

	if ((error = select_serial_row(stmt, &srow))) {
		goto fail;
	}

	/*
	 * XXX disable; causes `stmt`s current row to be freed even if there's no
	 * more rows -- this will invalidate borrowed strings before they can be
	 * dup'ed in the caller.
	 *
	 * The downside is that queries that are supposed to only select 1 row
	 * might now have silently multiple.
	 */
#if 0
	// verify exactly 1 row was returned
	error = sqlite3_step(stmt);
	if (error != SQLITE_DONE) {
		cf_print_err("file-lookup step 2 returned %d\n", error);
		// XXX return an errno, not a sql code
		goto fail;
	}
#endif // 0
	error = 0;
fail:
	return error;
}

/*
 * Wrapper around sqlite3_step() for a single lookup query.
 *
 * The purpose is to remap error codes:
 * - 0: success
 * - ENOENT: no entry
 * - sql error: some other failure
 */
static int
query_step_one(sqlite3_stmt *stmt)
{
	const int error = sqlite3_step(stmt);

	if (error == SQLITE_ROW) {
		return 0;
	}

	if (error == SQLITE_DONE) {
		return ENOENT;
	}

	// some other error
	cf_print_err("lookup execution failed %d\n", error);

	// note: sqlite3 error domain; not errno
	return error;
}

/*
 * Serialize `path` into a sql query for a lookup into the file table.
 *
 * A table describing the mapping from sql columns to arguments, as the sql
 * type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * string   path         path, len
 */
static int
bind_file_lookup(sqlite3_stmt *stmt, const char *path, size_t len)
{
	const size_t num_columns = file_lookup_query.base.num_columns;

	column_val_t vals[num_columns];
	cf_str_borrow(path, len, &vals[0].str_val);

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = file_lookup_query.base.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Serialize `rowid` into a sql query for a lookup by ID the file table.
 *
 * A table describing the mapping from sql columns to arguments, as the sql
 * type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * int64    id           rowid
 */
static int
bind_file_id_lookup(sqlite3_stmt *stmt, int64_t rowid)
{
	const size_t num_columns = file_id_lookup_query.base.num_columns;

	column_val_t vals[num_columns];
	vals[0].uint64_val = rowid;

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = file_id_lookup_query.base.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Serialize `path` into a sql query for insertion into the file table.
 *
 * A table describing the mapping from sql columns to arguments, as the sql
 * type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * null     id           NULL
 * string   path         path, len
 */
static int
bind_file_insert(sqlite3_stmt *stmt, const char *path, size_t len)
{
	const size_t num_columns = file_insert_query.num_columns;

	column_val_t vals[num_columns];
	vals[0].null_val = true;
	cf_str_borrow(path, len, &vals[1].str_val);

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = file_insert_query.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Serialize `rowid` into a sql query for a type lookup.
 *
 * A table describing the mapping from sql columns to arguments, as the sql
 * type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * int64    id           rowid
 */
static int
bind_type_lookup(sqlite3_stmt *stmt, int64_t rowid)
{
	const size_t num_columns = type_lookup_query.base.num_columns;

	column_val_t vals[num_columns];
	vals[0].uint64_val = rowid;

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = type_lookup_query.base.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Serialize the members of `entry` into a sql query.
 *
 * A table describing the mapping from sql columns to struct members, as well
 * as the sql type:
 *
 * index   |type    |SQL         |struct
 * --------|--------|------------|------
 * 1        null     typeid       NULL
 * 2        int      kind         entry->kind
 * 3        int      complete     entry->complete
 * 4        int64    file         loc->file
 * 5        int64    func         loc->func
 * 6        int      scope        loc->scope
 * 7        int      line         loc->line
 * 8        int      column       loc->column
 */
static int
bind_type_insert(sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_type_entry_t *entry)
{
	const size_t num_columns = type_insert_query.num_columns;

	column_val_t vals[num_columns];
	vals[0].null_val = true;
	vals[1].uint32_val = entry->kind;
	vals[2].uint32_val = entry->complete;
	vals[3].uint64_val = loc->file.rowid;
	vals[4].uint64_val = loc->func.rowid;
	vals[5].uint32_val = loc->scope;
	vals[6].uint32_val = loc->line;
	vals[7].uint32_val = loc->column;

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = type_insert_query.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Format `stmt` to do a lookup using `entry` as a key.
 *
 * A table describing the mapping from sql columns to struct members, as well
 * as the sql type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * int64    file         loc->file
 * string   name         name->name.{str,len}
 */
static int
bind_typename_lookup(sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_typename_t *name)
{
	const size_t num_columns = typename_lookup_query.base.num_columns;

	column_val_t vals[num_columns];
	vals[0].uint64_val = (uint64_t)loc->file.rowid;
	cf_str_borrow_str(&name->name, &vals[1].str_val);

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = typename_lookup_query.base.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Format `stmt` to do a search for typenames matching `name`.
 *
 * A table describing the mapping from sql columns to struct members, as well
 * as the sql type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * string   name         name->{str,len}
 */
static int
bind_typename_find(sqlite3_stmt *stmt, const cf_str_t *name)
{
	const size_t num_columns = typename_find_query.base.num_columns;

	column_val_t vals[num_columns];
	cf_str_borrow_str(name, &vals[0].str_val);

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = typename_find_query.base.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Serialize the members of `name` into a sql query.
 *
 * A table describing the mapping from sql columns to struct members, as well
 * as the sql type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * string   name         name->name.{str,len}
 * int      kind         name->kind
 * int64    base_type    name->base_type
 * int64    file         loc->file
 * int64    func         loc->func
 * int      scope        loc->scope
 * int      line         loc->line
 * int      column       loc->column
 */
static int
bind_typename_insert(sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_typename_t *name)
{
	const size_t num_columns = typename_insert_query.num_columns;

	column_val_t vals[num_columns];
	cf_str_borrow_str(&name->name, &vals[0].str_val);
	vals[1].uint32_val = name->kind;
	vals[2].uint64_val = (uint64_t)name->base_type.rowid;
	vals[3].uint64_val = (uint64_t)loc->file.rowid;
	vals[4].uint64_val = (uint64_t)loc->func.rowid;
	vals[5].uint32_val = loc->scope;
	vals[6].uint32_val = loc->line;
	vals[7].uint32_val = loc->column;

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = typename_insert_query.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Serialize `entry` into a sql query for insertion into the type use table.
 *
 * A table describing the mapping from sql columns to arguments, as the sql
 * type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * int64    base_type    entry->base_type
 * int      kind         entry->kind
 * int64    file         loc->file
 * int      line         loc->line
 * int      column       loc->column
 */
static int
bind_type_use_insert(sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_type_use_t *entry)
{
	const size_t num_columns = type_use_insert_query.num_columns;

	column_val_t vals[num_columns];
	vals[0].uint64_val = (uint64_t)entry->base_type.rowid;
	vals[1].uint32_val = entry->kind;
	vals[2].uint64_val = (uint64_t)loc->file.rowid;
	vals[3].uint32_val = loc->line;
	vals[4].uint32_val = loc->column;

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = type_use_insert_query.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Serialize `entry` into a sql query for insertion into the member table.
 *
 * A table describing the mapping from sql columns to arguments, as the sql
 * type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * int64    parent       entry->parent
 * int64    base_type    entry->base_type
 * string   name         entry->name
 * int64    file         loc->file
 * int      line         loc->line
 * int      column       loc->column
 */
static int
bind_member_insert(sqlite3_stmt *stmt, const loc_ctx_t *loc,
		const db_member_t *entry)
{
	const size_t num_columns = member_insert_query.num_columns;

	column_val_t vals[num_columns];
	vals[0].uint64_val = (uint64_t)entry->parent.rowid;
	vals[1].uint64_val = (uint64_t)entry->base_type.rowid;
	cf_str_borrow_str(&entry->name, &vals[2].str_val);
	vals[3].uint64_val = loc->file.rowid;
	vals[4].uint32_val = loc->line;
	vals[5].uint32_val = loc->column;

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = member_insert_query.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Format `stmt` to do a member lookup using `parent` and `name` as a key.
 *
 * A table describing the mapping from sql columns to struct members, as well
 * as the sql type:
 *
 * type    |SQL         |arg
 * --------|------------|------
 * int64    parent       parent
 * string   name         name->{str,len}
 */
static int
bind_member_lookup(sqlite3_stmt *stmt, int64_t parent, const cf_str_t *name)
{
	const size_t num_columns = member_lookup_query.base.num_columns;

	column_val_t vals[num_columns];
	vals[0].uint64_val = (uint64_t)parent;
	cf_str_borrow_str(name, &vals[1].str_val);

	const serial_row_t row = {
		.num_columns = num_columns,
		.column_kinds = member_lookup_query.base.column_kinds,
		.column_values = vals,
	};

	return bind_serial_row(stmt, &row);
}

/*
 * Bind `stmt` according to `row`.
 *
 * Each entry in `row->column_values[i]` is bound to `stmt` with a data type
 * specified by `row->column_kind[i]`.
 */
static int
bind_serial_row(sqlite3_stmt *stmt, const serial_row_t *row)
{
	int error;
	cf_assert(row->num_columns);
	cf_assert(row->num_columns < INT_MAX);

	for (size_t i = 0; i < row->num_columns; ++i) {
		// note: sqlite bind indices start at 1
		const int bind_index = i + 1;
		error = bind_one_column(stmt, bind_index,
				row->column_kinds[i], &row->column_values[i]);
		if (error) {
			cf_print_err("cannot bind index %d, kind %u, error %d\n",
					bind_index, row->column_kinds[i], error);
			// stop early
			break;
		}
	}
	return error;
}

/*
 * With `stmt` as an "insert" query, bind `val` as the `bind_index`-numbered
 * argument.
 *
 * Do type conversion from `column_val_t`s unsigned types to sqlite's signed
 * types.
 */
static int
bind_one_column(sqlite3_stmt *stmt, int bind_index, column_kind_t column_kind,
		const column_val_t *val)
{
	switch (column_kind) {
		case column_null:
			return sqlite3_bind_null(stmt, bind_index);
		case column_uint32:
			if (val->uint32_val > INT_MAX) {
				return ERANGE;
			}
			return sqlite3_bind_int(stmt, bind_index, (int)val->uint32_val);
		case column_uint64:
			if (val->uint64_val > INT64_MAX) {
				return ERANGE;
			}
			return sqlite3_bind_int64(stmt, bind_index,
					(int64_t)val->uint64_val);
		case column_str:
			return sqlite3_bind_text(stmt, bind_index,
					val->str_val.str, cf_str_len(&val->str_val),
					SQLITE_STATIC);
	}
	cf_panic("unknown column kind %d\n", column_kind);
}

/*
 * The inverse of bind_serial_row().
 *
 * NOTE: unlike most functions with an `out` parameter, this function requires
 * that `*out` be partially initialized:
 * - num_columns
 *   Must be set to the expected number of columns.
 * - column_kinds
 *   Must point to an array of the expected column types.
 * - column_values
 *   Point to an uninitialized array `num_columns` large. This function will
 *   initialize from the values in `stmt`.
 */
static int
select_serial_row(sqlite3_stmt *stmt, const serial_row_t *out)
{
	int error;
	cf_assert(out->num_columns);
	cf_assert(out->column_kinds);
	cf_assert(out->column_values);

	// check `stmt` has right number of output columns, being careful to
	// convert from `int` to `size_t`
	const int num_columns = sqlite3_column_count(stmt);
	if ((num_columns <= 0) || ((size_t)num_columns != out->num_columns)) {
		cf_print_err("wrong number of output columns, got %d, expected %zu\n",
				num_columns, out->num_columns);
		error = EILSEQ;
		goto fail;
	}

	// write each `stmt` output column into the correct union variant of
	// `out->column_values`
	for (size_t i = 0; i < out->num_columns; ++i) {
		const int unbind_index = i;
		error = select_one_column(stmt, unbind_index,
				out->column_kinds[i], &out->column_values[i]);
		if (error) {
			cf_print_err("cannot unbind index %d, error %d\n",
					unbind_index, error);
			goto fail;
		}
	}

fail:
	return error;
}

/*
 * With `stmt` as an already-executed select statement, extract the `i`th
 * column into `out`.
 *
 * Do type conversion. sqlite stores signed integers and unsigned chars on disk
 * -- which is the opposite of what cfind uses.
 *
 * Note: if the column is a string, `out->str_val` is borrowed. It has lifetime
 * only as long as `stmt`. Calling either sqlite3_step() or sqlite3_finalize()
 * on `stmt` will invalidate the string returned by this function.
 */
static int
select_one_column(sqlite3_stmt *stmt, int index, column_kind_t expected_kind,
		column_val_t *out)
{
	// check the sql column type
	const int column_type = sqlite3_column_type(stmt, index);
	const int expected_type = sql_column_kind2type(expected_kind);
	if (column_type != expected_type) {
		cf_print_corrupt(
				"column %d has wrong type: got %d, expected %d(%u)\n",
				index, column_type, expected_type, expected_kind);
		return EILSEQ;
	}

	// switch on `column_kind_t` because sqlite types are generic
	// (SQLITE_INTEGER is used for int32 and int64)
	switch (expected_kind) {
		case column_null:
			// doesn't matter
			out->null_val = true;
			break;
		case column_uint32: {
			const int val = sqlite3_column_int(stmt, index);
			if (val < 0) {
				cf_print_corrupt("column %d int32 value out of range %d\n",
						index, val);
				// integer convert it anyway
			}
			out->uint32_val = (uint32_t)val;
			break;
		}
		case column_uint64: {
			const int64_t val = sqlite3_column_int64(stmt, index);
			if (val < 0) {
				cf_print_corrupt("column %d int64 value out of range %lld\n",
						index, p_(val));
			}
			out->uint64_val = (uint64_t)val;
			break;
		}
		case column_str: {
			const unsigned char *const str = sqlite3_column_text(stmt, index);
			const int len = sqlite3_column_bytes(stmt, index);
			if (len <= 0) {
				cf_print_corrupt("column %d string value has bad length %d\n",
						index, len);
				cf_str_null(&out->str_val);
			} else {
				cf_print_info("str-column borrow %p,%d '%.*s'\n",
						str, len,
						len, str);
				cf_str_borrow((char *)str, (size_t)len, &out->str_val);
			}
			break;
		}
	}

	return 0;
}

/*
 * Convert cfind `column_kind_t` to the corresponding sqlite type (an `int`).
 */
static int
sql_column_kind2type(column_kind_t kind)
{
	switch (kind) {
		case column_null:
			return SQLITE_NULL;
		case column_uint32:
			CF_FALLTHROUGH;
		case column_uint64:
			return SQLITE_INTEGER;
		case column_str:
			return SQLITE_TEXT;
	}
	cf_panic("unknown column kind %d\n", kind);
}

/*
 * Query compilation functions.
 */

#define CREATE_TABLE_BASE "CREATE TABLE IF NOT EXISTS "

static sqlite3_stmt *
compile_file_table_create(sqlite3 *db)
{
#define FILE_TABLE_QUERY_CREATE \
	CREATE_TABLE_BASE \
	FILE_TABLE_NAME " " \
	FILE_COLUMNS ";"
	return compile_query(db, FILE_TABLE_QUERY_CREATE);
}

static sqlite3_stmt *
compile_type_table_create(sqlite3 *db)
{
#define TYPE_TABLE_QUERY_CREATE \
	CREATE_TABLE_BASE \
	TYPE_TABLE_NAME " " \
	TYPE_COLUMNS ";"
	return compile_query(db, TYPE_TABLE_QUERY_CREATE);
}

static sqlite3_stmt *
compile_typename_table_create(sqlite3 *db)
{
#define TYPENAME_TABLE_QUERY_CREATE \
	CREATE_TABLE_BASE \
	TYPENAME_TABLE_NAME " " \
	TYPENAME_COLUMNS ";"
	return compile_query(db, TYPENAME_TABLE_QUERY_CREATE);
}

static sqlite3_stmt *
compile_incomplete_type_table_create(sqlite3 *db)
{
#define INCOMPLETE_TYPE_TABLE_QUERY_CREATE \
	CREATE_TABLE_BASE \
	INCOMPLETE_TYPE_TABLE_NAME " " \
	INCOMPLETE_TYPE_COLUMNS ";"
	return compile_query(db, INCOMPLETE_TYPE_TABLE_QUERY_CREATE);
}

static sqlite3_stmt *
compile_type_use_table_create(sqlite3 *db)
{
#define TYPE_USE_TABLE_QUERY_CREATE \
	CREATE_TABLE_BASE \
	TYPE_USE_TABLE_NAME " " \
	TYPE_USE_COLUMNS ";"
	return compile_query(db, TYPE_USE_TABLE_QUERY_CREATE);
}

static sqlite3_stmt *
compile_member_table_create(sqlite3 *db)
{
#define MEMBER_TABLE_QUERY_CREATE \
	CREATE_TABLE_BASE \
	MEMBER_TABLE_NAME " " \
	MEMBER_COLUMNS ";"
	return compile_query(db, MEMBER_TABLE_QUERY_CREATE);
}

static sqlite3_stmt *
compile_file_table_lookup(sqlite3 *db)
{
	return compile_query_desc(db, &file_lookup_query.base);
}

static sqlite3_stmt *
compile_file_table_id_lookup(sqlite3 *db)
{
	return compile_query_desc(db, &file_id_lookup_query.base);
}

static sqlite3_stmt *
compile_file_table_insert(sqlite3 *db)
{
	return compile_query_desc(db, &file_insert_query);
}

static sqlite3_stmt *
compile_type_table_lookup(sqlite3 *db)
{
	return compile_query_desc(db, &type_lookup_query.base);
}

static sqlite3_stmt *
compile_type_table_insert(sqlite3 *db)
{
	return compile_query_desc(db, &type_insert_query);
}

static sqlite3_stmt *
compile_typename_table_lookup(sqlite3 *db)
{
	return compile_query_desc(db, &typename_lookup_query.base);
}

static sqlite3_stmt *
compile_typename_table_find(sqlite3 *db)
{
	return compile_query_desc(db, &typename_find_query.base);
}

static sqlite3_stmt *
compile_typename_table_insert(sqlite3 *db)
{
	return compile_query_desc(db, &typename_insert_query);
}

static sqlite3_stmt *
compile_incomplete_type_table_lookup(sqlite3 *db)
{
	return compile_query_desc(db, &type_lookup_query.base);
}

static sqlite3_stmt *
compile_type_use_table_insert(sqlite3 *db)
{
	return compile_query_desc(db, &type_use_insert_query);
}

static sqlite3_stmt *
compile_member_table_insert(sqlite3 *db)
{
	return compile_query_desc(db, &member_insert_query);
}

static sqlite3_stmt *
compile_member_table_lookup(sqlite3 *db)
{
	return compile_query_desc(db, &member_lookup_query.base);
}

/*
 * Compile a query from a query description.
 *
 * Similar to macro compile_query(), this checks that all query descriptions
 * come from section QUERY_SECTION_NAME. However, it has to be a runtime check
 * because a `query_desc_t::query` is a `char *` rather than a string literal.
 */
static sqlite3_stmt *
compile_query_desc(sqlite3 *db, const query_desc_t *query)
{
	const bool in_range =
			(query_section_start <= (const char *)query) &&
			((const char *)query < query_section_end);
	if (!in_range) {
		cf_panic("query pointer %p not in section '%s'\n",
				query, QUERY_SECTION_NAME);
	}

	const size_t len = strlen(query->query) + 1;
	return compile_query_(db, query->query, len);
}

/*
 * Compile a sql query.
 *
 * The returned query can be bound and executed. Follow with a call to
 * sqlite3_finalize() to free it.
 */
static sqlite3_stmt *
compile_query_(sqlite3 *db, const char *query, size_t len_)
{
	cf_assert(len_ < INT_MAX); // `len` cast to `int` below
	const int len = (int)len_;

	int error;
	sqlite3_stmt *out = NULL;

	if ((error = sqlite3_prepare_v2(db, query, len, &out, NULL))) {
		// queries have to be valid sql at build time
		cf_print_debug("prepare_v2(%p, %p, %d, %p, %p) -> %d\n",
				db, query, len, &out, ((void*)NULL), error);
		cf_panic("cannot compile query '%.*s', error %d, "
				"extended %d/'%s'/'%s'\n",
				len, query, error, sqlite3_extended_errcode(db),
				sqlite3_errstr(error), sqlite3_errmsg(db));
	}
	cf_assert(out);
	return out;
}
