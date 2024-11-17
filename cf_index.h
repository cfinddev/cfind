/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Main API for creating an index.
 */
#include "cc_support.h"
#include "cf_db.h"

__BEGIN_DECLS

/*
 * Indexer configuration.
 *
 * This is basically `argv` parsed into a structure. It specifies the inputs to
 * the indexer.
 *
 * Members:
 * - db_kind
 *   The type of database written to when indexing.
 *   - index_db_nop
 *     If set, the indexer stores nothing (not to memory, not to disk). This is
 *     useful for dry run testing indexer code.
 *   - index_db_mem
 *     The index is written to an in-memory database, which is then discarded.
 *   - index_db_sql
 *     The index is written to a new sqlite database. Member `db_args.sql_path`
 *     specifies the filesystem path to create.
 *   - index_db_borrowed
 *     The database is injected by the caller via `db_args.db`. This is useful
 *     for tests that index then inspect the results.
 * - input_kind
 *   This specifies what `input_path` is. Note: nothing other than filesystem
 *   inputs is supported (because libclang). Tests need to conjure up a path to
 *   something if they want to use in-memory source inputs.
 *   - input_comp_db
 *     If set, the input is the path to the parent directory of a compilation
 *     database. E.g., if the compilation db is at "foo/compile_commands.json",
 *     set `input_path` to "foo".
 *   - input_source_file
 *     If set, the input is a single source file. Default compiler arguments
 *     are used for building the AST.
 *  - input_path
 *    Filesystem path to source. A ".c" file, or the parent directory of a
 *    compilation database, 
 */
typedef struct {
	enum {
		index_db_nop = 1,
		index_db_mem = 2,
		index_db_sql = 3,
		index_db_borrowed = 4,
	} db_kind;

	enum {
		input_comp_db = 1,
		input_source_file = 2,
	} input_kind;

	union {
		const char *sql_path;
		cf_db_t *db;
	} db_args;

	const char *input_path;
} index_config_t;

int cf_index_project(const index_config_t *config);

__END_DECLS
