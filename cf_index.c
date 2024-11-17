/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Core indexing code. Uses libclang to create ASTs.
 */
#include "cf_index.h"

#include "cc_support.h"
#include "cf_print.h"
#include "cf_assert.h"
#include "cf_alloc.h"
#include "cf_string.h"
#include "cf_vector.h"
#include "cf_map.h"
#include "db_types.h"
#include "cf_db.h"
#include "index_types.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/param.h>

#include "clang-c/Index.h"
#include "clang-c/CXString.h"
#include "clang-c/CXFile.h"
#include "clang-c/CXSourceLocation.h"
#include "clang-c/CXCompilationDatabase.h"

CF_VEC_FUNC_DECL(cursor_stack_t, CXCursor, cursor_stack);

CF_VEC_FUNC_DECL(struct_vec_t, struct_pkg_t, struct_vec);
CF_VEC_FUNC_DECL(memberpkg_vec_t, member_pkg_t, memberpkg_vec);
CF_VEC_FUNC_DECL(typeusepkg_vec_t, type_use_pkg_t, typeusepkg_vec);

CF_VEC_ITER_GENERATE(struct_vec_t, struct_pkg_t, struct_iter);
CF_VEC_ITER_GENERATE(memberpkg_vec_t, member_pkg_t, memberpkg_iter);
CF_VEC_ITER_GENERATE(typeusepkg_vec_t, type_use_pkg_t, typeusepkg_iter);

/*
 * Lightweight argument struct used in index_includes().
 */
typedef struct {
	cf_db_t *db;
	cf_map8_t *file_map;
	int error;
} include_ctx_t;

/*
 * Command line arguments to clang to compile a source file into an AST.
 *
 * This is basically the struct representation of
 *   $ clang -c foo.c
 *
 * This serves as an adaptor from from `CXCompileCommand` to
 * clang_parseTranslationUnit() arguments.
 *
 * Members
 * - n
 *   Length of `arg_data` and `argv` arrays.
 * - path_data
 *   Backing storage for `path`.
 * - arg_data
 *   Array of strings. Backing storage for the *strings* pointed to by the
 *   elements of `argv`.
 * - path
 *   Filesystem path to the main ".c" file to compile. This member is the
 *   simple `char *` version of `path_data`. This is borrowed; it must not be
 *   freed.
 * - argv
 *   Command line arguments used to compile `path`. This is an (owned) array of
 *   pointers to strings borrowed from `arg_data`.
 */
typedef struct {
	unsigned n;
	CXString path_data;
	CXString *arg_data;
	const char *path;
	const char *const *argv;
} argv_builder_t;

/*
 * XXX experimental
 * bad name
 *
 * stripped down sub-context for indexing the children of a struct
 *
 * it's gross to write
 *   type_stack_push(&ctx->struct_ctx->hidden->path->here);
 * it's seems better just to pull out the relevent structures up front
 */
typedef struct {
	index_ctx_t *ctx; // XXX strip down further
	cursor_stack_t *anon_type_stack;
	cursor_stack_t *parent_stack;
} index_children_ctx_t; // XXX unrelated to iterate_children()

/*
 * big argument structure to iterate_children()
 *
 * Members:
 * - path
 * - cb
 *   Callback called on every AST node.
 * - final
 *   Special 'finalizer' callback invoked when recursion for a node has
 *   completed.
 * - real_ctx
 */
typedef struct {
	ast_path_t *path;
	CXCursorVisitor cb;
	void (*final)(CXCursor parent, void *ctx);
	void *real_ctx;
} iterate_children_args_t;

/*
 * Enum for the three possible kinds of name a tag type may have.
 *
 * Each enumerator is used for the examples listed beneath it:
 * - struct_name_direct`
 *   - struct foo {};
 *   - struct foo {} my_foo;
 * - `struct_name_unnamed`
 *   - typedef struct {} foo_t;
 *   - struct {} my_foo;
 *   - enum {...};
 *   - global/function scope `struct {};`
 *   - but *not* for `struct foo {} my_foo`; 
 * - `struct_name_anon`
 *   - C11 record decls nested in another record
 *     `struct foo { >>>struct { }; };`
 *   - but *not* for global scope `struct {};`
 *   - not used for enums
 */
typedef enum {
	struct_name_direct = 1,
	struct_name_unnamed = 2,
	struct_name_anon = 3,
} struct_name_kind_t;

/*
 * Sub argument structure used in index_struct_children()
 */
typedef struct {
	index_ctx_t *ctx;
	struct_scoreboard_t *sb;
} index_struct_args_t;

// top-level indexing
static int index_project(const index_config_t *config, index_ctx_t *ctx);
static int index_target(const index_config_t *config, index_ctx_t *ctx,
		argv_builder_t *args);

static int index_compile_cmd(CXCompileCommand cmd,
		const index_config_t *config, index_ctx_t *ctx);
static int index_source(const index_config_t *config, index_ctx_t *ctx);
static int index_includes(CXTranslationUnit tu, index_ctx_t *ctx);
static int index_tu(CXTranslationUnit tu, index_ctx_t *ctx);

// generic iterators
static int iterate_children(CXCursor root, iterate_children_args_t *args);
static enum CXChildVisitResult iterate_children_cb(
		CXCursor cursor, CXCursor parent, CXClientData ctx);

static void index_include_cb(CXFile included_file,
		CXSourceLocation *inclusion_stack, unsigned include_len,
		CXClientData ctx);
static enum CXChildVisitResult index_ast_node_(
		CXCursor cursor, CXCursor parent, CXClientData ctx);

static enum CXChildVisitResult index_type_children_cb2(
		CXCursor cursor, CXCursor parent, void *sb);

// core indexing
static enum CXChildVisitResult index_ast_node(
		CXCursor cursor, CXCursor parent, index_ctx_t *ctx);
static void index_typedef(CXCursor cursor, index_ctx_t *ctx);
static bool index_struct(CXCursor cursor, index_ctx_t *ctx);
static void index_struct_record(CXCursor struct_decl, struct_scoreboard_t *sb);
static void index_struct_children(CXCursor cursor, index_ctx_t *ctx,
		struct_scoreboard_t *sb);
static enum CXChildVisitResult index_type_children_cb(CXCursor cursor,
		CXCursor parent, CXClientData args);
static void index_struct_finalizer(CXCursor cursor, void *args);

static bool special_index_struct_name(CXCursor cursor, struct_scoreboard_t *sb,
		index_ctx_t *ctx);

// struct scoreboard
static void make_struct_scoreboard(struct_scoreboard_t *out);
static void free_struct_scoreboard(struct_scoreboard_t *sb);
static void reset_struct_scoreboard(struct_scoreboard_t *sb);
static int commit_struct_scoreboard(struct_scoreboard_t *sb, index_ctx_t *ctx);
static void free_struct_scoreboard_rsrc(struct_scoreboard_t *sb);

static int commit_one_struct(struct_pkg_t *pkg, cf_map8_t *new_type_map,
		index_ctx_t *ctx);
static int commit_one_member(member_pkg_t *pkg, index_ctx_t *ctx);
static int commit_one_type_use(type_use_pkg_t *pkg, index_ctx_t *ctx);
static int struct_scoreboard_add_name(CXCursor cursor, struct_scoreboard_t *sb,
		index_ctx_t *ctx);
static void extract_typedef_name(CXCursor cursor, CXString *out);
static void extract_var_name(CXCursor cursor, CXString *out);

static bool translate_member_type(cf_map8_t *map1, cf_map8_t *map2,
		clang_type_t old, type_ref_t *out);
static bool translate_struct_type(cf_map8_t *map, clang_type_t old,
		type_ref_t *out);

static void index_member(CXCursor cursor, CXCursor parent,
		struct_scoreboard_t *sb);
static void build_member_record(CXCursor cursor, CXCursor parent,
		struct_scoreboard_t *sb);
static void maybe_build_typename(CXCursor cursor, struct_scoreboard_t *sb);
static void build_member_type_use(CXCursor cursor, CXCursor parent,
		struct_scoreboard_t *sb);
static void extract_member_typename(CXCursor member_decl, db_typename_t *out);

static void print_scoreboard_stats(const struct_scoreboard_t *sb);

// indexing misc.
static bool cursor_is_indexable(CXCursor cursor, index_ctx_t *ctx);
static bool user_type_is_indexable(CXCursor cursor);
static bool typedef_is_indexable(CXCursor cursor);
static bool var_is_indexable(CXCursor cursor);
static bool type_is_indexable(CXType ct);
static void update_location(index_ctx_t *ctx, CXCursor cursor);
static void assert_is_tag(enum CXCursorKind kind);

// extractor functions
static type_kind_t extract_type_kind(enum CXCursorKind kind);
static void extract_struct(CXCursor cursor, CXType ct,
		db_type_entry_t *entry_out);
static struct_name_kind_t extract_struct_name(CXCursor cursor, CXType ct,
		db_typename_t *name_out);
static void extract_member_name(CXCursor cursor, CXString *out);
static struct_name_kind_t get_struct_name_kind(CXCursor cursor);

// `index_ctx_t` functions
static int make_index_ctx(const index_config_t *config, index_ctx_t *out);
static int make_index_ctx_db(const index_config_t *config, index_ctx_t *out);
static void free_index_ctx(index_ctx_t *ctx);
static void reset_tu_ctx(index_ctx_t *ctx);

// maps
static void type_map_insert(cf_map8_t *map, clang_type_t ct,
		type_ref_t type_ref);
static bool type_map_lookup(cf_map8_t *map, CXType ct, type_ref_t *ref_out);
static bool type_map_lookup2(cf_map8_t *map, clang_type_t ct,
		type_ref_t *ref_out);
static void file_map_add(cf_map8_t *map, CXFile file, file_ref_t ref);
static bool file_map_lookup(cf_map8_t *map, CXFile file, file_ref_t *ref_out);

// ast path
static void make_ast_path(ast_path_t *out);
static void free_ast_path(ast_path_t *path);
static void reset_ast_path(ast_path_t *out);

// cursor stack
static CXCursor *cursor_stack_top(cursor_stack_t *stack);
static bool cursor_stack_descend(cursor_stack_t *stack, CXCursor cursor);

// `argv_builder_t`
static int command_argv_builder(CXCompileCommand cmd, argv_builder_t *out);
static void free_argv_builder(argv_builder_t *args);

// clang
static clang_type_t get_clang_type(CXType ct);

/*
 * Index the project/source file specified by `config`.
 *
 * Steps:
 * - make an `index_ctx_t`
 * - dispatch into either
 *   - index_project() if `config` contains a "compile_commands.json"
 *   - index_source() if `config` contains just a single ".c" file
 */
int
cf_index_project(const index_config_t *config)
{
	int error;
	index_ctx_t ctx;

	// make an indexing context to keep state between TUs
	if ((error = make_index_ctx(config, &ctx))) {
		goto fail;
	}

	if (config->input_kind == input_comp_db) {
		// index the compilation database specified in `config->input_path`
		error = index_project(config, &ctx);
	} else {
		// index single source file
		error = index_source(config, &ctx);
	}

	if (error) {
		goto fail_index;
	}

fail_index:
	free_index_ctx(&ctx);
fail:
	return error;
}

/*
 * Index all targets in a project.
 *
 * This is different from cf_index_project() in that it doesn't make `ctx`, and
 * a compilation database specifies the files to index.
 *
 * XXX passing in the parent directory of a compilation database is
 * counterintuitive. probably just change this to:
 * - pass in the path to a compilation database
 * - strip off the last component: dirname(1)
 * - pass into clang
 *
 * Note: do not confuse a compilation database with cfind's search database:
 * - compilation database
 *   A ".json" file that specifies how to compile every source file in a
 *   project. It's passed in via `ctx->input_path`. (Despite the name, there's
 *   nothing database-like about it at all.)
 * - cfind search database
 *   A newly created sqlite3 db. It was instantiated by the caller and passed
 *   in via `ctx->db`.
 */
static int
index_project(const index_config_t *config, index_ctx_t *ctx)
{
	int error = 0;

	// load compilation db from `config->input_path`
	CXCompilationDatabase_Error db_error = CXCompilationDatabase_NoError;
	CXCompilationDatabase db = clang_CompilationDatabase_fromDirectory(
			config->input_path, &db_error);
	if (db_error) {
		// `CXCompilationDatabase_Error` uses 1 error code for everything
		error = ESRCH;
		cf_print_debug("cannot load compilation db, error %d\n", db_error);
		goto fail;
	}

	CXCompileCommands cmds =
			clang_CompilationDatabase_getAllCompileCommands(db);
	const unsigned n = clang_CompileCommands_getSize(cmds);

	cf_print_info("loaded comp-db '%s'/compile_commands.json; %u commands\n",
			config->input_path, n);

	// for each target
	for (unsigned i = 0; i < n; ++i) {
		if ((error = index_compile_cmd(
				clang_CompileCommands_getCommand(cmds, i), config, ctx))) {
			goto fail_index;
		}
		// get rid of TU-specific state in `ctx`
		reset_tu_ctx(ctx);
	}

fail_index:
	clang_CompileCommands_dispose(cmds);
	clang_CompilationDatabase_dispose(db);
fail:
	return error;
}

/*
 * Index the target specified by `cmd`.
 */
static int
index_compile_cmd(CXCompileCommand cmd, const index_config_t *config,
		index_ctx_t *ctx)
{
	int error;

	// turn the target's compile arguments into a `char **`
	// XXX might need to do something with `cmd`s "working directory"
	argv_builder_t cmd_args;
	if ((error = command_argv_builder(cmd, &cmd_args))) {
		goto fail;
	}

	// pass `cmd_args.argv` into clang TU parser
	error = index_target(config, ctx, &cmd_args);
	if (error) {
		cf_print_debug("failed to index input '%s', error %d\n",
				cmd_args.path, error);
		goto fail_index;
	}

fail_index:
	free_argv_builder(&cmd_args);
fail:
	return error;
}

/*
 * Convert `cmd` into an `argv_builder_t`.
 *
 * Steps:
 * - build a vec of CXString
 *   from each arg in `cmd`
 *   this is not used directly; just to own string storage
 * - build a `char **` from vector
 *   this is passed into libclang
 */
static int
command_argv_builder(CXCompileCommand cmd, argv_builder_t *out)
{
	int error;

	const unsigned n = clang_CompileCommand_getNumArgs(cmd);
	cf_assert((size_t)n < (SIZE_MAX / sizeof(CXString)));

	CXString path_data;
	const char *path;
	CXString *arg_data;
	const char **argv;

	// array of n `CXString`s for args
	if (!(arg_data = cf_malloc(n * sizeof(CXString)))) {
		error = errno;
		cf_assert(error);
		goto fail;
	}

	// array of n `char *`s pointing to the `CXString`s
	if (!(argv = cf_malloc(n * sizeof(char *)))) {
		error = errno;
		cf_assert(error);
		goto fail2;
	}

	// build an array of each arg string
	for (unsigned i = 0; i < n; ++i) {
		arg_data[i] = clang_CompileCommand_getArg(cmd, i);
		argv[i] = clang_getCString(arg_data[i]);
	}

	path_data = clang_CompileCommand_getFilename(cmd);
	path = clang_getCString(path_data);

	*out = (argv_builder_t) {
		.n = n,
		.path_data = path_data,
		.arg_data = arg_data,
		.argv = argv,
		.path = path,
	};

	return 0;
fail2:
	cf_free(arg_data);
fail:
	return error;
}

/*
 * Free `args` created by a previous successful command_argv_builder().
 */
static void
free_argv_builder(argv_builder_t *args)
{
	clang_disposeString(args->path_data);
	cf_free((char **)args->argv);

	for (unsigned i = 0; i < args->n; ++i) {
		clang_disposeString(args->arg_data[i]);
	}
	cf_free(args->arg_data);
}

/*
 * Compile and index a single source file specified in `config`.
 *
 * This a wrapper to index_target() that uses default compile args.
 */
static int
index_source(const index_config_t *config, index_ctx_t *ctx)
{
	// default compile args
	static const char *const argv[] = {
		"clang",
		"-std=c17",
		"-x",
		"c",
	};

	// fake an `argv_builder_t` to call into index_target()
	argv_builder_t cmd_args = {
		.n = ARRAY_LEN(argv),
		.path = config->input_path,
		.argv = argv,
	};

	// compile and index
	return index_target(config, ctx, &cmd_args);
	// note: don't free `cmd_args` because it owns nothing
}

/*
 * Compile `args` and index it.
 */
static int
index_target(const index_config_t *config, index_ctx_t *ctx,
		argv_builder_t *args)
{
	(void)config;
	int error;
	CXTranslationUnit tu;

	// compile `args` into an AST
	const enum CXErrorCode cerror = clang_parseTranslationUnit2FullArgv(
			ctx->clang_index,
			args->path,
			args->argv, args->n,
			NULL, 0, CXTranslationUnit_None, &tu);

	if (cerror) {
		cf_print_err("cannot make TU from '%s', error %d\n",
				args->path, cerror);
		error = -1; // XXX need better error
		goto fail;
	}

	cf_print_info("made TU %p for '%s'; %u args\n", tu, args->path, args->n);

	// index `#include`s to get the source files involved
	if ((error = index_includes(tu, ctx))) {
		cf_print_err("failed to index includes error %d\n", error);
		goto fail_index;
	}

	// index AST itself
	if ((error = index_tu(tu, ctx))) {
		goto fail_index;
	}

fail_index:
	clang_disposeTranslationUnit(tu);
fail:
	return error;
}

static void
make_struct_scoreboard(struct_scoreboard_t *out)
{
	make_ast_path(&out->path);
	cursor_stack_make(&out->current_parent_stack);

	struct_vec_make(&out->new_types);
	memberpkg_vec_make(&out->members);
	typeusepkg_vec_make(&out->type_uses);
	cf_map8_make(&out->unnamed_types);
}

static void
free_struct_scoreboard(struct_scoreboard_t *sb)
{
	free_ast_path(&sb->path);
	cursor_stack_free(&sb->current_parent_stack);

	free_struct_scoreboard_rsrc(sb);

	struct_vec_free(&sb->new_types);
	memberpkg_vec_free(&sb->members);
	typeusepkg_vec_free(&sb->type_uses);
	cf_map8_free(&sb->unnamed_types);
}

/*
 * Make `sb` *look* new, but don't force vector reallocation when `sb` is
 * reused.
 */
static void
reset_struct_scoreboard(struct_scoreboard_t *sb)
{
	reset_ast_path(&sb->path);
	cursor_stack_reset(&sb->current_parent_stack);

	// free external resources held by vectors
	free_struct_scoreboard_rsrc(sb);

	struct_vec_reset(&sb->new_types);
	memberpkg_vec_reset(&sb->members);
	typeusepkg_vec_reset(&sb->type_uses);
	cf_map8_reset(&sb->unnamed_types);
}

static void
free_struct_scoreboard_rsrc(struct_scoreboard_t *sb)
{
	// free struct names
	cf_vec_iter_t struct_it;
	struct_iter_make(&sb->new_types, &struct_it);
	while (struct_iter_next(&struct_it)) {
		struct_pkg_t *pkg = struct_iter_peek(&struct_it);
		// XXX sdklfj might be in unnamed types list
		cf_str_free(&pkg->name.name);
	}
	struct_iter_free(&struct_it);

	// free member names
	cf_vec_iter_t member_it;
	memberpkg_iter_make(&sb->members, &member_it);
	while (memberpkg_iter_next(&member_it)) {
		member_pkg_t *pkg = memberpkg_iter_peek(&member_it);
		cf_str_free(&pkg->entry.name);
	}
	memberpkg_iter_free(&member_it);

	// note: `type_uses` holds no external resources
}

/*
 * Serialize in-memory state in `sb` to `ctx`.
 *
 * Note:
 * - types may or may not preexist in the database
 *   be careful about reinserting x
 *
 * Steps:
 * - build a new type map (clang::Type* -> rowid)
 * - iterate over `sb->new_types`
 *   discard entries in `sb->unnamed_types`
 *   serialize (type-entry, name) into `ctx->db`
 *     if it's new, store the new rowid in the *new* type map
 *     if it's old, store the preexisting rowid in `ctx->type_map`
 * - iterate over `sb->members`
 *   translate parent `type_ref_t::p` to rowid with the new type map *only*
 *   look up referenced types with both type maps
 * - merge new type map into old type map
 */
static int
commit_struct_scoreboard(struct_scoreboard_t *sb, index_ctx_t *ctx)
{
	print_scoreboard_stats(sb);

	cf_map8_t new_type_map;
	cf_map8_make(&new_type_map);

	cf_vec_iter_t new_types_it;
	struct_iter_make(&sb->new_types, &new_types_it);

	// serialize all new types
	while (struct_iter_next(&new_types_it)) {
		struct_pkg_t *pkg = struct_iter_peek(&new_types_it);
		const clang_type_t type_id = pkg->type_id;
		cf_assert(type_id);

		cf_print_info("serialize struct %p\n", type_id);

		uint64_t dummy;
		if (cf_map8_lookup(&sb->unnamed_types, (uintptr_t)type_id, &dummy)) {
			// skip unnamed types
			cf_print_warn("type id %p has no name\n", type_id);
			continue;
		}

		// commit pkg, updating `new_type_map` if it's new
		(void)commit_one_struct(pkg, &new_type_map, ctx);
	}
	struct_iter_free(&new_types_it);

	// serialize all non-type decls in `sb`

	// members
	cf_vec_iter_t member_it;
	memberpkg_iter_make(&sb->members, &member_it);

	while (memberpkg_iter_next(&member_it)) {
		member_pkg_t *pkg = memberpkg_iter_peek(&member_it);

		// translate pkg->parent with new type map only
		if (!translate_struct_type(&new_type_map, pkg->parent,
				&pkg->entry.parent)) {
			// possibly unnamed or a bug
			continue;
		}

		// translate base type with either map
		clang_type_t old_base_type = pkg->entry.base_type.p;
		if (!translate_member_type(&ctx->type_map, &new_type_map,
				old_base_type, &pkg->entry.base_type)) {
			cf_print_err("no db entry for member base type %p\n",
					old_base_type);
			continue;
		}

		(void)commit_one_member(pkg, ctx);
	}
	memberpkg_iter_free(&member_it);

	// uses
	cf_vec_iter_t type_uses_it;
	typeusepkg_iter_make(&sb->type_uses, &type_uses_it);
	while (typeusepkg_iter_next(&type_uses_it)) {
		type_use_pkg_t *pkg = typeusepkg_iter_peek(&type_uses_it);

		// look up parent to make sure it's new
		type_ref_t dummy;
		if (!translate_struct_type(&new_type_map, pkg->where, &dummy)) {
			// parent isn't new, skip
			continue;
		}

		// translate base type with either type map
		// XXX bad function name
		clang_type_t use_type = pkg->entry.base_type.p;
		if (!translate_member_type(&new_type_map, &ctx->type_map, use_type,
				&pkg->entry.base_type)) {
			cf_print_err("cannot find db entry for type use type %p\n",
					use_type);
			continue;
		}

		(void)commit_one_type_use(pkg, ctx);
	}
	typeusepkg_iter_free(&type_uses_it);

	// now merge `new_type_map` into `ctx->type_map`
	cf_vec_iter_t new_type_it;
	cf_vec_iter_make(&new_type_map.v, &new_type_it);
	while (cf_vec_iter_next(&new_type_it)) {
		cf_map_entry_t *entry = cf_vec_iter_peek(&new_type_it);

		// insert current entry in type map
		type_map_insert(&ctx->type_map,
				(clang_type_t)entry->key,
				(type_ref_t){.rowid = entry->value});
	}
	cf_vec_iter_free(&new_type_it);

	cf_map8_free(&new_type_map);

	return 0; // XXX ???
}

/*
 * Steps:
 * - check for a preexisting entry according to `pkg->name`
 *   if it preexists, add to `ctx`s "old" type map
 *   no new database entries will be created
 * - insert typename_entry_t then type_entry_t into database
 * - save new rowid in `new_type_map`
 */
static int
commit_one_struct(struct_pkg_t *pkg, cf_map8_t *new_type_map, index_ctx_t *ctx)
{
	int error;
	type_ref_t struct_ref;

	error = cf_db_typename_lookup(ctx->db, &pkg->loc[1], &pkg->name,
			&struct_ref);
	if (!error) {
		// preexists, mutate old type map
		type_map_insert(&ctx->type_map, pkg->type_id, struct_ref);
		goto fail;
	} else if (error != ENOENT) {
		// some other error; can't determine if the struct preexists
		goto fail;
	}
	// error == ENOENT: struct is new

	// add type entry to database
	if ((error = cf_db_type_insert(ctx->db, &pkg->loc[0], &pkg->entry,
			&struct_ref))) {
		cf_print_err("cannot insert type (id %p, kind %d) to db, error %d\n",
				pkg->type_id, pkg->entry.kind, error);
		goto fail;
	}

	// mutate typename to reference the new type entry
	memcpy(&pkg->name.base_type, &struct_ref, sizeof(type_ref_t));

	// add typename to reference it
	if ((error = cf_db_typename_insert(ctx->db, &pkg->loc[1], &pkg->name))) {
		cf_print_err("cannot add primary typename "
				"(id %p, rowid %lld, name '%.*s') to db, error %d\n",
				pkg->type_id, p_(struct_ref.rowid),
				(int)cf_str_len(&pkg->name.name), pkg->name.name.str,
				error);
		goto fail_name;
	}

	type_map_insert(new_type_map, pkg->type_id, struct_ref);
	return 0;
fail_name:
	// XXX type entry inserted above is leaked here
fail:
	return error;
}

/*
 * Translate base type of a member entry.
 *
 * Translate `old` with either `map1` or `map2`. On success, return true and
 * write the db ref to `*out`.
 */
static bool
translate_member_type(cf_map8_t *map1, cf_map8_t *map2, clang_type_t old,
		type_ref_t *out)
{
	if (!old) {
		// NULL is used for primitive types
		out->rowid = 0;
		return true;
	}

	if (translate_struct_type(map1, old, out)) {
		return true;
	}
	return translate_struct_type(map2, old, out);
}

static bool
translate_struct_type(cf_map8_t *map, clang_type_t old, type_ref_t *out)
{
	uint64_t new;
	if (!cf_map8_lookup(map, (uintptr_t)old, &new)) {
		return false;
	}
	out->rowid = (int64_t)new;
	return true;
}

/*
 * Steps:
 * - XXX just insert it?
 */
static int
commit_one_member(member_pkg_t *pkg, index_ctx_t *ctx)
{
	cf_assert(pkg->entry.parent.p);
	// zero for primitives
	// cf_assert(pkg->entry.base_type.p);
	return cf_db_member_insert(ctx->db, &pkg->loc, &pkg->entry);
}

/*
 * Steps:
 * - XXX just insert it?
 */
static int
commit_one_type_use(type_use_pkg_t *pkg, index_ctx_t *ctx)
{
	cf_assert(pkg->entry.base_type.p);
	cf_assert(pkg->entry.kind);
	return cf_db_type_use_insert(ctx->db, &pkg->loc, &pkg->entry);
}

/*
 * `cursor` is a typedef or variable decl for the primary struct in `sb`.
 */
static int
struct_scoreboard_add_name(CXCursor cursor, struct_scoreboard_t *sb,
		index_ctx_t *ctx)
{
	cf_assert(struct_vec_len(&sb->new_types));

	// (hack) primary struct is always at index 0
	struct_pkg_t *entry = struct_vec_at(&sb->new_types, 0);
	cf_assert(entry);
	const enum CXCursorKind cursor_kind = clang_getCursorKind(cursor);

	// double check it's in the unnamed types map
	uint64_t index;
	if (cf_map8_lookup(&sb->unnamed_types, (uintptr_t)entry->type_id, &index)) {
		cf_assert(index == 0);
	} else {
		// missing
		cf_panic("tried to add name to already-named struct %p\n",
				entry->type_id);
	}

	CXString name;
	typename_kind_t name_kind;

	// extract `cursor`-specific name string
	if (cursor_kind == CXCursor_VarDecl) {
		extract_var_name(cursor, &name);
		name_kind = name_kind_var;
	} else if (cursor_kind == CXCursor_TypedefDecl) {
		extract_typedef_name(cursor, &name);
		name_kind = name_kind_typedef;
	} else {
		cf_print_err("_add_name with non-name cursor %u\n", cursor_kind);
		return EILSEQ;
	}

	// initialize name in `entry`
	db_typename_t *name_entry = &entry->name;
	memset(name_entry, 0, sizeof(*name_entry));
	name_entry->kind = name_kind;
	name_entry->base_type.p = NULL; // doesn't matter

	// copy name string
	const char *c_string = clang_getCString(name);
	cf_str_dup(c_string, strlen(c_string), &name_entry->name);
	clang_disposeString(name);

	memcpy(&entry->loc[1], &ctx->loc, sizeof(loc_ctx_t));

	// finally remove from unnamed types map
	cf_map8_remove(&sb->unnamed_types, (uintptr_t)entry->type_id);

	return 0;
}

/*
 * Hide a many line printf that reports statistics per a single struct indexed.
 */
static void
print_scoreboard_stats(const struct_scoreboard_t *sb)
{
	cf_print_debug(
			"commit %zu types, %zu members, %zu uses."
			" %u total records, %zu nameless\n",
			struct_vec_len(&sb->new_types), memberpkg_vec_len(&sb->members),
			typeusepkg_vec_len(&sb->type_uses), sb->path.count,
			cf_map8_len(&sb->unnamed_types));
}

/*
 */
static int
index_includes(CXTranslationUnit tu, index_ctx_t *ctx)
{
	include_ctx_t sub_ctx = {
		.db = ctx->db,
		.file_map = &ctx->file_map,
		.error = 0,
	};
	// call out to index_include_cb() on each include in `tu`
	clang_getInclusions(tu, index_include_cb, &sub_ctx);

	// propagate any error during iteration
	return sub_ctx.error;
}

static void
nop(CF_UNUSED CXCursor parent, CF_UNUSED void *ctx)
{
}

/*
 * Used as a callback in index_includes().
 *
 * try the following:
 * - look at `included_file`
 * - insert into on-disk db
 * - build an in-memory map
 *   map from (fsid -> rowid)
 *
 * note: this may be called multiple times for different TUs.
 *
 * XXX consider using something other than `index_ctx_t`
 *   only two members are used
 */
static void
index_include_cb(CXFile included_file, CXSourceLocation *inclusion_stack,
		unsigned include_len, CXClientData ctx_)
{
	(void)inclusion_stack;
	(void)include_len;
	int error;
	include_ctx_t *ctx = ctx_;

	CXString name = clang_getFileName(included_file);
	CXFileUniqueID id;
	clang_getFileUniqueID(included_file, &id);

	cf_print_info("include '%s', %p, fsid={%llu, %llu, %llu}\n",
			clang_getCString(name), included_file,
			id.data[0], id.data[1], id.data[2]);

	// check if it already exists (perhaps from a previous TU)
	file_ref_t ref;
	if (file_map_lookup(ctx->file_map, included_file, &ref)) {
		// already seen; skip it
		cf_print_debug("skipped adding include '%s', rowid %ld\n",
				clang_getCString(name), ref.rowid);
		goto fail;
	}

	// file is new

	// add to db
	const char *c_string = clang_getCString(name);
	if ((error = cf_db_add_file(ctx->db, c_string, strlen(c_string), &ref))) {
		cf_print_debug("cannot add #include file '%s', error %d\n",
				c_string, error);
		ctx->error = error;
		goto fail;
	}
	// track the mapping from file ID -> rowid
	cf_print_info("map file %p->%ld\n", included_file, ref.rowid);
	file_map_add(ctx->file_map, included_file, ref);

fail:
	clang_disposeString(name);
}

/*
 * Index all children of translation unit `tu` and mutate `ctx`.
 */
static int
index_tu(CXTranslationUnit tu, index_ctx_t *ctx)
{
	int error = 0;

	// make a cursor starting at the root of the TU
	CXCursor root = clang_getTranslationUnitCursor(tu);
	if (clang_Cursor_isNull(root)) {
		cf_print_err("can't get TU root node\n");
		error = -2;
		goto fail;
	}

	cf_print_info("starting iteration\n");

	iterate_children_args_t args = {
		.path = &ctx->path,
		.cb = index_ast_node_,
		.final = nop,
		.real_ctx = ctx,
	};
	(void)iterate_children(root, &args);

	cf_print_info("iteration complete, found %d nodes\n", ctx->path.count);
fail:
	return error;
}

/*
 */
static int
iterate_children(CXCursor root, iterate_children_args_t *args)
{
	int error;

	// add `root` as the bottom-most parent of the path stack
	if (!cursor_stack_descend(&args->path->parent_stack, root)) {
		cf_print_err("push root cursor -> ENOMEM\n");
		error = ENOMEM;
		goto fail;
	}

	(void)clang_visitChildren(root, iterate_children_cb, args);

	// pop root
	CXCursor *top = cursor_stack_pop_start(&args->path->parent_stack);
	cursor_stack_pop_end(&args->path->parent_stack, top);

	error = 0;
fail:
	return error;
}

/*
 * When AST iteration (maybe) ascends, pop elements from `parent_stack` until
 * `parent` is found.
 *
 * look at parent, cursor and parent_stack
 * parent - same as before
 *   no depth change
 * parent ... in parent_stack
 */
static enum CXChildVisitResult
iterate_children_cb(CXCursor cursor, CXCursor parent, CXClientData ctx_)
{
	iterate_children_args_t *ctx = ctx_;
	cursor_stack_t *parent_stack = &ctx->path->parent_stack;
	ctx->path->count++;

	// compute new depth
	while (!clang_equalCursors(*cursor_stack_top(parent_stack), parent)) {
		CXCursor *top = cursor_stack_pop_start(parent_stack);
		if (!top) {
			// parent not found
			cf_print_err("parent %p not in stack\n", parent.data[0]);
			break;
		}

		// signal completion of recursion
		ctx->final(*top, ctx->real_ctx);

		cursor_stack_pop_end(parent_stack, top);
	}

	// do real work with cb()
	enum CXChildVisitResult ret = ctx->cb(cursor, parent, ctx->real_ctx);

	// look at return value, recurse -> new level
	// push `cursor` as the top-most parent
	if (ret == CXChildVisit_Recurse) {
		if (!cursor_stack_descend(&ctx->path->parent_stack, cursor)) {
			// ??? XXX parent will be wrong
		}
	}
	return ret;
}

/*
 * Get the c++ `clang::Type*` value from libclang wrapper `ct`.
 *
 * This is an abstraction leak from libclang. However, there's no interface to
 * get a unique value from a `CXType` -- it can only test for equality with
 * clang_equalTypes(). This isn't so useful for making a fast data structure to
 * map from `clang::Type*` -> sql rowid.
 *
 * Only use canonical types. This makes the clang type for `struct foo` and
 * `foo` the same.
 */
static clang_type_t
get_clang_type(CXType ct)
{
	cf_assert(ct.kind != CXType_Invalid);
	CXType canon = clang_getCanonicalType(ct);
	cf_assert(canon.kind != CXType_Invalid);
	return canon.data[0];
}

/*
 * Wrapper to hide `CXClientData` -> `index_ctx_t` cast.
 */
static enum CXChildVisitResult
index_ast_node_(CXCursor cursor, CXCursor parent, CXClientData ctx)
{
	return index_ast_node(cursor, parent, ctx);
}

/*
 * Callback invoked per top-level AST node of a TU.
 *
 * similar to ASTConsumer::HandleTopLevelDecl()
 * only direct children are visited by default
 *
 * Used in index_tu().
 *
 * NOTE: only used for global scope. Function and struct children use different
 * callbacks.
 *
 * Steps:
 * - skip non-indexable nodes
 * - update source location in `ctx->loc`
 * - dispatch to sub-indexing function based on the type of node
 * - for structs:
 *   Indexing an unnamed struct can require inspecting 2 nodes at the same
 *   level. E.g.,
 *     C snippet
 *       typedef struct {} foo_t;
 *     turns into 2 ast nodes:
 *       record-decl 123 "<unnamed>"
 *       typedef 123 "foo_t"
 *
 *     The first node is passed to index_struct(). The return value signals
 *     whether the node indexed on next iteration should be treated as a
 *     potential name. In this case, a non-NULL `index_ctx_t::last_struct`
 *     value is used. On the next iteration, the name node is passed to
 *     special_index_struct_name() to specially handle it as the name of the
 *     previous structure. However, this might not succeed in which case the
 *     node is just indexed like normal.
 */
static enum CXChildVisitResult
index_ast_node(CXCursor cursor, CF_UNUSED CXCursor parent, index_ctx_t *ctx)
{
	enum CXChildVisitResult ret = CXChildVisit_Recurse;

	// check if `cursor` is worth indexing
	if (!cursor_is_indexable(cursor, ctx)) {
		ret = CXChildVisit_Continue;
		goto fail;
	}

	// get its new source location
	update_location(ctx, cursor);

	const bool need_name = ctx->last_struct;
	const enum CXCursorKind kind = clang_getCursorKind(cursor);

	// check if the node after a struct decl might be its name
	if (need_name) {
		const bool skip = special_index_struct_name(
				cursor, &ctx->struct_sb, ctx);

		// commit and reset regardless of whether struct has a name
		// reset `ctx` and commit scoreboard to the database
		(void)commit_struct_scoreboard(&ctx->struct_sb, ctx);
		reset_struct_scoreboard(&ctx->struct_sb);
		ctx->last_struct = (clang_type_t)0;

		if (skip) {
			// current node was already indexed as a struct name
			ret = CXChildVisit_Continue;
			goto fail;
		}
		// special indexing failed, try to index like normal
	}

	// dispatch to an indexer specific to the kind of `cursor`
	switch (kind) {
		case CXCursor_StructDecl:
		case CXCursor_UnionDecl:
		case CXCursor_EnumDecl:
			if (index_struct(cursor, ctx)) {
				// need name
				ctx->last_struct = get_clang_type(clang_getCursorType(cursor));
				cf_print_info("look for struct %p name next iter\n",
						ctx->last_struct);
			}
			ret = CXChildVisit_Continue;
			break;
		case CXCursor_TypedefDecl:
			index_typedef(cursor, ctx);
			break;
		case CXCursor_FieldDecl:
			// error: not allowed at global scope
			CF_FALLTHROUGH;
		case CXCursor_FunctionDecl:
		case CXCursor_VarDecl:
		case CXCursor_MemberRefExpr:
		default:
			// unimplemented
			break;
	}

fail:
	return ret;
}

/*
 * Return true if `cursor` was successfully indexed as the name for the
 * uncommitted struct in `sb`.
 */
static bool
special_index_struct_name(CXCursor cursor, struct_scoreboard_t *sb,
		index_ctx_t *ctx)
{
	cf_assert(ctx->last_struct);

	const clang_type_t last_struct = ctx->last_struct;
	const enum CXCursorKind kind = clang_getCursorKind(cursor);

	// extract the struct type cursor refers to
	clang_type_t cursor_type;
	if (kind == CXCursor_VarDecl) {
		cursor_type = get_clang_type(
				clang_getCanonicalType(clang_getCursorType(cursor)));
	} else if (kind == CXCursor_TypedefDecl) {
		cursor_type = get_clang_type(clang_getCanonicalType(
				clang_getTypedefDeclUnderlyingType(cursor)));
	} else {
		// an unnamed struct must be followed by either a typedef or a var
		// warn about no substitute name
		cf_print_warn("bad code: struct %p does not declare anything\n",
				last_struct);
		return false;
	}

	// XXX make sure this works for pointer var decls
	if (last_struct != cursor_type) {
		cf_print_warn("expected struct var/typedef decl for '%p', got '%p'\n",
				last_struct, cursor_type);

		return false;
	}

	// add `cursor` as the name of the struct in `sb`
	(void)struct_scoreboard_add_name(cursor, sb, ctx);
	return true;
}

/*
 * Determine whether `cursor` is worth indexing.
 *
 * This function only acts as a coarse grain filter within index_ast_node().
 * Sub-indexing functions have more context on whether a node is indexable.
 */
static bool
cursor_is_indexable(CXCursor cursor, index_ctx_t *ctx)
{
	(void)ctx;
	switch (clang_getCursorKind(cursor)) {
		case CXCursor_StructDecl:
		case CXCursor_UnionDecl:
		case CXCursor_EnumDecl:
			return user_type_is_indexable(cursor);
		case CXCursor_TypedefDecl:
			return typedef_is_indexable(cursor);
		case CXCursor_VarDecl:
			return var_is_indexable(cursor);
		case CXCursor_FunctionDecl:
		case CXCursor_MemberRefExpr:
			// XXX unimplemented
			return false;
		case CXCursor_UnexposedDecl:
			// never indexable
		default:
			return false;
	}
	__builtin_unreachable();
}

/*
 * Return true if cursor, which is a user-defined type decl, is indexable.
 *
 * index_struct() has more context on whether a type is indexable. This
 * function is used to filter out top level nodes -- most of which is
 * indexable.
 *
 * Prohibit the following:
 * - incomplete types (because they're unimplemented)
 */
static bool
user_type_is_indexable(CXCursor cursor)
{
	CXType ct = clang_getCursorType(cursor);
	cf_assert(type_is_indexable(ct));

	const bool incomplete = clang_Type_getAlignOf(ct) ==
			CXTypeLayoutError_Incomplete;

	return !incomplete;
}

/*
 * Return true if cursor, which is a typedef decl, is indexable.
 *
 * Prohibit the following:
 * - typedefs of primitive types
 */
static bool
typedef_is_indexable(CXCursor cursor)
{
	CXType old_type =
			clang_getCanonicalType(clang_getTypedefDeclUnderlyingType(cursor));

	return type_is_indexable(old_type);
}

/*
 * Return true if a cursor for a global variable declaration is indexable.
 *
 * True for variables of struct/union/enum type.
 */
static bool
var_is_indexable(CXCursor cursor)
{
	CXType var_type = clang_getCanonicalType(clang_getCursorType(cursor));
	return type_is_indexable(var_type);
}

/*
 * Return true if clang type `ct` is indexable.
 *
 * This is true for
 * - struct
 * - union
 * - enum
 * and notably *not* true for
 * - builtin types (int)
 * - elaborated types
 *   the clang::Type for `struct foo` versus that of just `foo`
 *   (XXX `CXType_Elaborated` might not be used in the C AST)
 */
static bool
type_is_indexable(CXType ct)
{
	return (ct.kind == CXType_Record) || (ct.kind == CXType_Enum);
}

/*
 * Update `ctx->loc` to the source location of `cursor`.
 *
 * Steps:
 * - extract file from `cursor`
 *   print when file changes
 * - extract the line/column
 * - ignore function and scope level for now
 */
static void
update_location(index_ctx_t *ctx, CXCursor cursor)
{
	CXSourceRange range = clang_getCursorExtent(cursor);
	CXSourceLocation loc = clang_getRangeStart(range);

	if (clang_Range_isNull(range)) {
		// shouldn't happen, but worth checking
		cf_print_err("null range\n");
	}

	CXFile file;
	unsigned line;
	unsigned column;

	clang_getExpansionLocation(loc, &file, &line, &column, /*offset=*/NULL);

	// check if the current file changed
	file_ref_t file_ref;
	if (!file_map_lookup(&ctx->file_map, file, &file_ref)) {
		// NOTE: all files in a TU should have already been seen during
		// index_includes()
		cf_print_err("no file entry for %p\n", file);
		goto fail;
	}

	if (ctx->loc.file.rowid != file_ref.rowid) {
		// file changed; update it in `ctx`
		cf_print_info("file changed from %lld to %lld\n",
				p_(ctx->loc.file.rowid), p_(file_ref.rowid));
		ctx->loc.file = file_ref;
	}

	// skip function/scope; it can't be updated here

	// update line/column
	ctx->loc.line = line;
	ctx->loc.column = column;

fail:
	return;
}

/*
 * cf_assert() that `kind` is a user-defined type.
 */
static void
assert_is_tag(enum CXCursorKind kind)
{
	switch (kind) {
		case CXCursor_StructDecl:
		case CXCursor_UnionDecl:
		case CXCursor_EnumDecl:
			return;
		default:
			cf_assert_fail("cursor %u isn't tag decl\n", kind);
	}
}

/*
 * Map from clang CXCursorKind to cfind `type_kind_t`.
 */
static type_kind_t
extract_type_kind(enum CXCursorKind kind)
{
	switch (kind) {
		case CXCursor_StructDecl:
			return type_kind_struct;
		case CXCursor_UnionDecl:
			return type_kind_union;
		case CXCursor_EnumDecl:
			return type_kind_enum;
		default:
			break;
	}
	cf_panic("unknown cursor type kind %d\n", kind);
}

/*
 * Build a `db_type_entry_t` from clang AST types.
 */
static void
extract_struct(CXCursor cursor, CXType ct, db_type_entry_t *entry_out)
{
	const type_kind_t entry_kind =
			extract_type_kind(clang_getCursorKind(cursor));
	// kind of a hack to test incompleteness
	const bool incomplete = clang_Type_getAlignOf(ct) ==
			CXTypeLayoutError_Incomplete;

	*entry_out = (db_type_entry_t) {
		.kind = entry_kind,
		.complete = !incomplete,
	};
}

/*
 * Mega big time hack to detect what kind of name a tag decl has.
 *
 * I.e., detect `typedef struct {...} foo_t;`
 *
 * Do the following to detect unnamed records:
 * - configure a special printing policy
 *   print tag keywords
 *   exclude tag definition
 *   skip member decls
 * - get a string for `cursor` according to printing policy
 * - compare cursor string with "struct {"
 *   unnamed types will match
 *   named types won't match because they're like "struct foo {"
 */
static struct_name_kind_t
get_struct_name_kind(CXCursor cursor)
{
	const enum CXCursorKind kind = clang_getCursorKind(cursor);
	assert_is_tag(kind);

	// check for C11 anonymous structs/unions
	if (clang_Cursor_isAnonymousRecordDecl(cursor)) {
		return struct_name_anon;
	}

	// do the hack described above to detect unnamed types
	CXPrintingPolicy policy = clang_getCursorPrintingPolicy(cursor);

	clang_PrintingPolicy_setProperty(policy,
			CXPrintingPolicy_SuppressTagKeyword, 0);
	clang_PrintingPolicy_setProperty(policy,
			CXPrintingPolicy_IncludeTagDefinition, 0);
	clang_PrintingPolicy_setProperty(policy,
			CXPrintingPolicy_TerseOutput, 1);

	CXString name_data = clang_getCursorPrettyPrinted(cursor, policy);
	const char *name = clang_getCString(name_data);

	bool unnamed;
	switch (kind) {
		case CXCursor_StructDecl:
			unnamed = !strncmp(name, "struct {", 8);
			break;
		case CXCursor_UnionDecl:
			unnamed = !strncmp(name, "union {", 7);
			break;
		case CXCursor_EnumDecl:
			unnamed = !strncmp(name, "enum {", 6);
			break;
		default:
			cf_panic("non-tag kind %d\n", kind);
	}

	clang_PrintingPolicy_dispose(policy);
	clang_disposeString(name_data);

	return unnamed ? struct_name_unnamed : struct_name_direct;
}

/*
 * Optionally get the name of struct decl `cursor`.
 *
 * The name kind is returned. The name string is conditionally returned via
 * `name_out` as a fully initialized typename entry.
 *
 * Name kind:
 * - unnamed or anonymous
 *   name *not* set
 * - direct
 *   name set to the tag string
 *
 * For direct name structs, `name_out->name` is set as an owned string. It
 * needs to be cf_str_free()ed.
 */
static struct_name_kind_t
extract_struct_name(CXCursor cursor, CF_UNUSED CXType ct,
		db_typename_t *name_out)
{
	CXString name;

	// return nothing for unnamed and anonymous structs
	const struct_name_kind_t kind = get_struct_name_kind(cursor);
	switch (kind) {
		case struct_name_unnamed:
		case struct_name_anon:
			return kind;
		case struct_name_direct:
			break;
	}

	// get name
	// name = clang_getTypeSpelling(ct); // old: gives "struct foo"
	// NOTE: gives "foo" from `struct foo`
	name = clang_getCursorSpelling(cursor);
	const char *c_string = clang_getCString(name);

	// build `name_out`
	memset(name_out, 0, sizeof(*name_out));
	name_out->kind = name_kind_direct;
	// NOTE: member `base_type` isn't used for direct names
	cf_str_dup(c_string, strlen(c_string), &name_out->name);

	clang_disposeString(name);
	return struct_name_direct;
}

/*
 * For a cursor that refers to the FieldDecl below,
 *   struct {
 *     int foo;
 *   }
 * return a string containing "foo".
 *
 * Ownership of the string is transfered to the caller. Follow with a call to
 * clang_disposeString() on the returned string.
 */
static void
extract_member_name(CXCursor cursor, CXString *out)
{
	// XXX not sure if right function
	*out = clang_getCursorSpelling(cursor);
}

/*
 * For a cursor that refers to a typedef like
 *   typedef ... foo_t;
 * return a string of the newly introduced typename "foo_t".
 *
 * Ownership of the string is transfered to the caller. Follow with a call to
 * clang_disposeString() on the returned string.
 */
static void
extract_typedef_name(CXCursor cursor, CXString *out)
{
	// XXX not sure if right function
	*out = clang_getCursorSpelling(cursor);
}

/*
 * For a cursor that refers to a variable declaration
 *   struct foo my_foo;
 * return a string of the variable name "my_foo".
 *
 * Ownership of the string is transfered to the caller. Follow with a call to
 * clang_disposeString() on the returned string.
 */
static void
extract_var_name(CXCursor cursor, CXString *out)
{
	// XXX not sure if right function
	*out = clang_getCursorSpelling(cursor);
}

/*
 * Given `cursor` that refers to a typedef AST node, index it.
 *
 * Steps:
 * - check `clang::Type*` already exists in the type map
 *   index_struct() must have already been called on the same type
 * - build a `db_typename_t` entry
 * - check for preexistence in the db
 *   if so, do nothing
 * - insert entry into database
 */
static void
index_typedef(CXCursor cursor, index_ctx_t *ctx)
{
	int error;

	// resolve old CXType to a database type reference
	type_ref_t old_ref;
	CXType old_type = clang_getCanonicalType(
			clang_getTypedefDeclUnderlyingType(cursor));

	if (!type_map_lookup(&ctx->type_map, old_type, &old_ref)) {
		// 3 reasons:
		// an incomplete type (XXX unimplemented)
		// this is a typedef of something not indexable (e.g. int)
		// a clang bug, a typedef appears before a decl
		cf_print_debug("cannot find type ref %p\n", get_clang_type(old_type));
		goto fail;
	}

	CXString name_data = clang_getTypedefName(clang_getCursorType(cursor));
	const char *c_string = clang_getCString(name_data);

	db_typename_t record;
	memset(&record, 0, sizeof(record));
	record.kind = name_kind_typedef;
	record.base_type = old_ref;
	cf_str_borrow(c_string, strlen(c_string), &record.name);

	// look up any preexisting entry
	type_ref_t db_entry_ref;
	error = cf_db_typename_lookup(ctx->db, &ctx->loc, &record, &db_entry_ref);

	if (!error) {
		// already exists
		if (db_entry_ref.rowid != old_ref.rowid) {
			// somehow found: `typedef A foo_t` vs `typedef B foo_t`
			cf_print_err("mismatched typedef '%s', old %lld, new %lld\n",
					c_string, p_(old_ref.rowid), p_(db_entry_ref.rowid));
			// keep the old type
		}
		goto fail_db;
	} else if (error != ENOENT) {
		// some other error
		cf_print_err("cannot look up typename '%s'\n", c_string);
		goto fail_db;
	}

	// error == ENOENT
	// entry is new, insert it
	error = cf_db_typename_insert(ctx->db, &ctx->loc, &record);

	if (error) {
		cf_print_err("can't persist typedef '%s', error %d\n",
				c_string, error);
		goto fail_db;
	}

	cf_print_info("added typedef '%s'->(%p, %lld)\n",
			c_string, get_clang_type(old_type), p_(old_ref.rowid));

fail_db:
	cf_str_free(&record.name);
	clang_disposeString(name_data);
fail:
	return;
}

/*
 * Given `cursor` that refers to a struct declaration AST node, index it and
 * its children nodes.
 *
 * Because indexing a structure involves inspecting a variable number of AST
 * nodes, a scoreboard is used to stage database updates. Entries are written
 * to `ctx->struct_sb`. At the end, the scoreboard is committed with
 * commit_struct_scoreboard() which will optionally insert entries into the
 * database, as well as the type map.
 *
 * In more depth, consider the following (valid c89) C source at global scope:
 *   struct {
 *     struct global {
 *       int a;
 *     };
 *     int garbage;
 *   };
 *
 * For this input, the goal of the indexer is to:
 * - create an entry for `struct global`
 * - discard any entries for the outer unnamed struct, "outer"
 *
 * The outer struct is not possible to look up because it has no identifier. It
 * shouldn't be inserted into the database. The challenge is that the indexer
 * won't know this until it finishes traversing all of the nodes. If the
 * indexer were to insert database entries as it encounters each node, it would
 * need to delete the records for "outer" after the fact.
 *
 * A database transaction won't help because entries that need to be discarded
 * are interleaved with entries that need to be saved. Re-iterating parts of
 * the AST is pain. Either iteration code needs to be duplicated, or special
 * cased to "traverse the same records but delete instead of insert".
 *
 * At the expense of extra memory use, entries created for a struct are staged
 * to a `struct_scoreboard_t` and then committed in pieces.
 *
 * Steps:
 * - build entry for top-level record decl from `cursor`
 * - recursively index children
 * - if `cursor` already has a name
 *   commit the scoreboard now; return false
 *   else:
 *   return true
 *   rely on index_ast_node() to treat the next sibling node as a name, and
 *   then commit
 */
static bool
index_struct(CXCursor cursor, index_ctx_t *ctx)
{
	struct_scoreboard_t *sb = &ctx->struct_sb;

	// scoreboard must not currently be in use
	cf_assert(!struct_vec_len(&sb->new_types));

	// could be a struct, union, enum
	CXType cursor_type = clang_getCanonicalType(clang_getCursorType(cursor));
	const clang_type_t type_id = get_clang_type(cursor_type);
	cf_assert(type_is_indexable(cursor_type));

	// index struct and children
	memcpy(&sb->loc, &ctx->loc, sizeof(loc_ctx_t)); // XXX hack
	index_struct_record(cursor, sb);
	index_struct_children(cursor, ctx, sb);

	// if `cursor` is a direct-name struct, commit scoreboard now
	// otherwise signal to caller to look for a name
	cf_assert(struct_vec_len(&sb->new_types));
	cf_assert(struct_vec_at(&sb->new_types, 0)->type_id == type_id);

	uint64_t index;
	if (cf_map8_lookup(&sb->unnamed_types, (uintptr_t)type_id, &index)) {
		// `cursor` is unnamed
		cf_assert(index == 0);
		return true;
	}
	// `cursor` already has a name
	commit_struct_scoreboard(sb, ctx);
	reset_struct_scoreboard(sb);
	return false;
}

/*
 * Index only the top-level record of a struct.
 *
 * three cases for struct name:
 * - direct name
 *   add name/location into record
 * - unnamed
 *   leave name empty
 *   add to unnamed_types vec
 * - anonymous
 *   not allowed at global/function scope
 *   discard record
 */
static void
index_struct_record(CXCursor struct_decl, struct_scoreboard_t *sb)
{
	struct_pkg_t record;

	CXType ct = clang_getCanonicalType(clang_getCursorType(struct_decl));
	cf_assert(type_is_indexable(ct));

	memset(&record, 0, sizeof(record));
	record.type_id = get_clang_type(ct);
	memcpy(&record.loc[0], &sb->loc, sizeof(loc_ctx_t));
	extract_struct(struct_decl, ct, &record.entry);

	// XXX incomplete structs aren't supported yet
	if (!record.entry.complete) {
		cf_print_warn("incomplete structs aren't supported\n");
		// continue on; even if the struct is later completed, its members
		// won't be updated
		record.entry.complete = true;
	}

	const struct_name_kind_t kind =
			extract_struct_name(struct_decl, ct, &record.name);

	cf_print_info("index '%s' record %p, name-kind %d\n",
			db_type_kind_str(record.entry.kind), record.type_id, kind);

	// anonymous types aren't indexed (but children are)
	if (kind == struct_name_anon) {
		// only allowed when nested in other records
		cf_assert(cursor_stack_len(&sb->current_parent_stack));
		return;
	}

	if (kind == struct_name_direct) {
		// for named structs, reuse the struct location for the name
		cf_assert(kind == struct_name_direct);
		memcpy(&record.loc[1], &record.loc[0], sizeof(loc_ctx_t));
	}

	// transfer ownership of `record` to new types vector
	if (!struct_vec_push(&sb->new_types, &record)) {
		cf_print_debug("can't push type record\n");
		goto fail;
	}

	if (kind == struct_name_unnamed) {
		// record in unnamed types map `clang::Type*` -> `new_types` index
		const size_t new_index = struct_vec_len(&sb->new_types);
		cf_assert(new_index);
		cf_map_entry_t *new_entry = cf_map8_reserve(&sb->unnamed_types);

		*new_entry = (cf_map_entry_t) {
			.key = (uintptr_t)record.type_id,
			.value = (uint64_t)new_index - 1,
		};

		cf_map8_commit(&sb->unnamed_types, new_entry);
	}

	return;
fail:
	cf_str_free(&record.name.name);
}

/*
 * indexing a whole struct from beginning to end:
 * - attempt to pull out name
 *   if unnamed, mark a name is later needed
 * - insert struct entry into type table
 * - index children
 *   keep a "parent type" stack for (true) anonymous types
 *   keep a "node" stack for AST dfs traversal
 *   index a node:
 *   - add entry for regular member variable
 *   - recursively index nested type decl
 *     this isn't exactly the same as inspect_struct2() though
 *     index_struct_children() indexes everything -- there is no split between
 *     indexing unnamed structs and their typedef/variable names
 *     it's also different in that there's no type forward decls
 *
 * ----
 * steps
 *
 * - prepare context
 *   - bottom-most path node is `cursor`
 *   - current type is `cursor`'s type
 *   - ??? something about a sub- typeref map
 *
 */
static void
index_struct_children(CXCursor struct_cursor, index_ctx_t *ctx,
		struct_scoreboard_t *sb)
{
	// add `struct_cursor` as the top-most type
	cursor_stack_push(&sb->current_parent_stack, &struct_cursor);

	index_struct_args_t real_ctx = {
		.ctx = ctx,
		.sb = sb,
	};

	iterate_children_args_t args = {
		.path = &sb->path,
		.cb = index_type_children_cb,
		.final = index_struct_finalizer,
		.real_ctx = &real_ctx,
	};

	// recursively index all children of this struct
	// XXX 2nd level call into clang_visitChildren()
	iterate_children(struct_cursor, &args);
}

/*
 * Called by iterate_children() when recursion of a struct's children have been
 * visited.
 *
 * Used in index_struct_children().
 *
 * This pops an entry from the `current_parent_stack` after recursion of a
 * struct's children completes. This skips popping when iteration of an
 * anonymous struct completes.
 */
static void
index_struct_finalizer(CXCursor cursor, void *args_)
{
	index_struct_args_t *args = args_;

	cursor_stack_t *stack = &args->sb->current_parent_stack;

	// if recursion completed for current type
	// then pop it too
	CXCursor *top = cursor_stack_top(stack);
	if (!top) {
		cf_print_err("index-type-children() empty type stack\n");
		return;
	}
	if (clang_equalCursors(cursor, *top)) {
		top = cursor_stack_pop_start(stack);
		cf_assert(top);
		cursor_stack_pop_end(stack, top);
	}
}

/*
 * Wrapper to index_type_children_cb2().
 *
 * Track start/end of nested types.
 */
static enum CXChildVisitResult
index_type_children_cb(CXCursor cursor, CXCursor parent, CXClientData args_)
{
	index_struct_args_t *args = args_;

	// get its new source location
	update_location(args->ctx, cursor);
	// XXX mega big hack, just copy ctx's location context into sb
	memcpy(&args->sb->loc, &args->ctx->loc, sizeof(loc_ctx_t));

	// do real indexing work
	const enum CXChildVisitResult ret =
			index_type_children_cb2(cursor, parent, args->sb);

	// push cursor when recursing for the children of a non-anonymous type
	// (don't worry about anonymous enums)
	CXType cursor_type = clang_getCursorType(cursor);
	bool new_parent =
			(cursor_type.kind != CXType_Invalid) &&
			type_is_indexable(cursor_type) /* && !isAnonymousRecordDecl() */;

	if (new_parent) {
		if (clang_Cursor_isAnonymousRecordDecl(cursor)) {
			cf_print_info("anonymous type %p, don't push to "
					"current_parent_stack\n", get_clang_type(cursor_type));
			new_parent = false;
		} else {
			new_parent = true;
		}
	}

	if ((ret == CXChildVisit_Recurse) && new_parent) {
		if (!cursor_stack_descend(&args->sb->current_parent_stack, cursor)) {
			// XXX ???
		}
	}
	return ret;
}

/*
 * Indexing callback for all children of a RecordDecl.
 *
 * A struct can have many children in addition to regular members.
 *
 * Do the following:
 * - struct
 *   recursive indexing
 *   adjust "current parent stack"
 *   use ctx->path.parent_stack
 * - anonymous struct
 *   don't index
 *   descend
 * - member
 *   insert into db under "current parent"
 */
static enum CXChildVisitResult
index_type_children_cb2(CXCursor cursor, CF_UNUSED CXCursor parent, void *sb_)
{
	struct_scoreboard_t *sb = sb_;
	enum CXChildVisitResult ret;

	switch (clang_getCursorKind(cursor)) {
		// not allowed nested under a type
		case CXCursor_UnexposedDecl:
		case CXCursor_FunctionDecl:
		case CXCursor_TypedefDecl:
		// case CXCursor_FirstAttr..CXCursor_LastAttr:
		default:
			// continue onto to next node
			ret = CXChildVisit_Continue;
			goto fail;

		// unimplemented enums
		case CXCursor_EnumConstantDecl:
		case CXCursor_EnumDecl:
			cf_print_info("nested enums unimplemented\n");
			ret = CXChildVisit_Continue;
			goto fail;

		// nested types
		case CXCursor_StructDecl:
		case CXCursor_UnionDecl:
			// index decl, then recurse
			// Note: call this function on the next iteration rather than a
			// recursive index_struct_children()
			index_struct_record(cursor, sb);
			ret = CXChildVisit_Recurse;
			break;

		// normal members
		case CXCursor_FieldDecl: {
			CXCursor *parent_type =
					cursor_stack_top(&sb->current_parent_stack);
			cf_assert(parent_type);
			// regular member/enumerator
			ret = CXChildVisit_Continue;
			index_member(cursor, *parent_type, sb);
			break;
		}
	}

fail:
	return ret;
}

/*
 * Index a single member/enumerator.
 *
 * ...
 * Do the following for a member:
 * - determine its parent struct/union
 *   That's the top of `anon_type_stack`.
 *  WRONG the parent is already passed in
 *
 * generate up to three records:
 * - member record
 * - typename 'var' (for an unnamed struct)
 * - type use 'decl'
 *
 */
static void
index_member(CXCursor cursor, CXCursor parent, struct_scoreboard_t *sb)
{
	const enum CXCursorKind kind = clang_getCursorKind(cursor);
	if (kind == CXCursor_EnumConstantDecl) {
		// XXX unimplemented
		return;
	}

	// struct/union member
	cf_assert(kind == CXCursor_FieldDecl);

	// regular member
	build_member_record(cursor, parent, sb);

	// check for typename record
	maybe_build_typename(cursor, sb);

	// record the use of `cursor`s type
	build_member_type_use(cursor, parent, sb);
}

/*
 * Build a member variable record `db_member_t`.
 *
 * XXX
 */
static void
build_member_record(CXCursor cursor, CXCursor parent, struct_scoreboard_t *sb)
{
	// get types
	CXType parent_type = clang_getCanonicalType(clang_getCursorType(parent));
	CXType member_type = clang_getCanonicalType(clang_getCursorType(cursor));

	clang_type_t member_clang_type;
	if (type_is_indexable(member_type)) {
		member_clang_type = get_clang_type(member_type);
	} else {
		// use NULL for any primitive type member
		member_clang_type = (clang_type_t)NULL;
	}

	// get name
	CXString name;
	extract_member_name(cursor, &name);
	const char *c_string = clang_getCString(name);

	// build record
	member_pkg_t record;
	memset(&record, 0, sizeof(record));

	// NOTE: in-memory `clang::Type*` is used instead of usual db rowid
	record.parent = get_clang_type(parent_type);
	record.entry = (db_member_t) {
		.parent = {
			.p = get_clang_type(parent_type)
		},
		.base_type = {
			.p = member_clang_type
		},
	};
	cf_str_dup(c_string, strlen(c_string), &record.entry.name);
	memcpy(&record.loc, &sb->loc, sizeof(loc_ctx_t));

	memberpkg_vec_push(&sb->members, &record);

	cf_print_info("index member '%s', type %p, parent %p\n",
			c_string, record.entry.base_type.p, record.entry.parent.p);

	clang_disposeString(name);
	// `record.entry.name` moved into vector
}

/*
 * Look for cursor in sb->unnamed_types
 *
 * XXX
 * what's here only works for regular members
 * check for variable-name-only structs too
 * the following needs to generate two records
 *   struct foo {
 *     int a; // works
 *     struct {
 *     } b; // half works
 *   };
 *
 * try the following:
 * - search `sb->unnamed_types` for `cursor`s type
 * - if there's a match
 *   edit the `struct_vec_t` name
 *   remove entry from unnamed_types vector
 *   build a db_typename_t record
 *
 * TODO
 * make sure this works for pointers/arrays
 *   struct foo {
 *     struct {
 *     } b[4];
 *   };
 *
 * probably do the following:
 * - ignore primitives
 * - call type_traverse()
 *   the result should be a struct/union
 * - check `unnamed_types` map
 */
static void
maybe_build_typename(CXCursor cursor, struct_scoreboard_t *sb)
{
	cf_assert(clang_getCursorKind(cursor) == CXCursor_FieldDecl);
	CXType clang_type = clang_getCanonicalType(clang_getCursorType(cursor));

	const uint64_t clang_type_id = (uintptr_t)get_clang_type(clang_type);
	uint64_t struct_index;

	if (!cf_map8_lookup(&sb->unnamed_types, clang_type_id, &struct_index)) {
		// no unnamed struct
		return;
	}
	// an entry matches

	cf_map8_remove(&sb->unnamed_types, clang_type_id);

	// add a name to the struct record
	cf_assert(struct_index <= SIZE_MAX);
	struct_pkg_t *unnamed_struct =
			struct_vec_at(&sb->new_types, (size_t)struct_index);
	cf_assert(unnamed_struct);

	extract_member_typename(cursor, &unnamed_struct->name);
	memcpy(&unnamed_struct->loc[1], &sb->loc, sizeof(loc_ctx_t));
}

/*
 * For a member variable declaration, build a `db_type_use_t`.
 *
 * Only index structs/union/(enums). Ignore primitive types.
 * `parent` is used to track the parent struct as a location in which the type
 * use appears.
 *
 * XXX make sure this works for pointers/arrays
 * possibly call type_traverse()
 */
static void
build_member_type_use(CXCursor cursor, CXCursor parent,
		struct_scoreboard_t *sb)
{
	CXType clang_type = clang_getCanonicalType(clang_getCursorType(cursor));
	CXType parent_type = clang_getCanonicalType(clang_getCursorType(parent));

	// don't index primitives
	if (!var_is_indexable(cursor)) {
		return;
	}

	// build record
	type_use_pkg_t entry;
	memset(&entry, 0, sizeof(entry));

	entry.where = get_clang_type(parent_type);
	entry.entry = (db_type_use_t) {
		.base_type = {
			.p = get_clang_type(clang_type)
		},
		.kind = type_use_decl,
	};
	memcpy(&entry.loc, &sb->loc, sizeof(loc_ctx_t));

	// add to scoreboard
	typeusepkg_vec_push(&sb->type_uses, &entry);

	cf_print_info("index type-use of %p within %p\n",
			get_clang_type(clang_type), get_clang_type(parent_type));
}

/*
 * Given a clang FieldDecl, build a variable-name-only typename record.
 *
 * XXX consider changing the schema so `db_typename_t` has a "parent type"
 * member that specifies scope. Right now the record is indistinguishable from
 * a global variable. This would require passing in the current parent struct.
 */
static void
extract_member_typename(CXCursor member_decl, db_typename_t *out)
{
	cf_assert(clang_getCursorKind(member_decl) == CXCursor_FieldDecl);

	CXString name;
	extract_member_name(member_decl, &name);

	memset(out, 0, sizeof(*out));
	out->kind = name_kind_var;
	out->base_type.p = NULL; // doesn't matter

	const char *c_string = clang_getCString(name);
	cf_str_dup(c_string, strlen(c_string), &out->name);
	clang_disposeString(name);
}

static void
type_map_insert(cf_map8_t *map, clang_type_t ct, type_ref_t type_ref)
{
	cf_assert(ct);
	cf_assert(type_ref.rowid);

	cf_map_entry_t *entry = cf_map8_reserve(map);
	entry->key = (uint64_t)ct;
	entry->value = (uint64_t)type_ref.rowid;
	cf_map8_commit(map, entry);
}

static bool
type_map_lookup(cf_map8_t *map, CXType ct, type_ref_t *ref_out)
{
	return type_map_lookup2(map, get_clang_type(ct), ref_out);
}

static bool
type_map_lookup2(cf_map8_t *map, clang_type_t ct, type_ref_t *ref_out)
{
	uint64_t val;
	if (!cf_map8_lookup(map, (uintptr_t)ct, &val)) {
		return false;
	}
	ref_out->rowid = val;
	return true;
}

/*
 * Insert into a map from `FileEntry *` -> rowid.
 *
 * This isn't stable between TUs
 *
 * XXX consider switching to a map of "file ID" -> rowid
 * but this requires a map with 128bit keys
 */
static void
file_map_add(cf_map8_t *map, CXFile file, file_ref_t ref)
{
	cf_map_entry_t *entry = cf_map8_reserve(map);
	if (!entry) {
		cf_print_err("cannot reserve file-map entry\n");
		return;
	}
	entry->key = (uint64_t)file;
	entry->value = (uint64_t)ref.rowid;
	cf_map8_commit(map, entry);
}

static bool
file_map_lookup(cf_map8_t *map, CXFile file, file_ref_t *ref_out)
{
	uint64_t val;
	if (!cf_map8_lookup(map, (uint64_t)file, &val)) {
		return false;
	}
	ref_out->rowid = val;
	return true;
}

static CXCursor *
cursor_stack_top(cursor_stack_t *stack)
{
	const size_t len = cursor_stack_len(stack);
	if (!len) {
		return NULL;
	}
	return cursor_stack_at(stack, len - 1);
}

/*
 * Call when AST iteration descends into the children of node.
 *
 * Push `parent` to the given stack. When AST iteration later ascends, call
 * cursor_stack_ascend().
 */
static bool
cursor_stack_descend(cursor_stack_t *parent_stack, CXCursor cursor)
{
	CXCursor *p = cursor_stack_reserve(parent_stack);
	if (!p) {
		cf_print_err("cursor_stack_reserve() -> ENOMEM\n");
		return false;
	}

	memcpy(p, &cursor, sizeof(cursor));
	cursor_stack_commit(parent_stack, p);
	return true;
}


/*
 * Initialize `out` to a new indexing context.
 *
 * On success, follow with a call to free_index_ctx().
 */
static int
make_index_ctx(const index_config_t *config, index_ctx_t *out)
{
	int error;

	memset(out, 0, sizeof(*out));

	// make a clang index; a "tu collection"
	out->clang_index = clang_createIndex(0, 1);

	// init datastructures
	cf_map8_make(&out->type_map);
	cf_map8_make(&out->file_map);

	make_ast_path(&out->path);
	make_struct_scoreboard(&out->struct_sb);

	// initialize database separately
	if ((error = make_index_ctx_db(config, out))) {
		goto fail;
	}

	return 0;
fail:
	free_ast_path(&out->path);
	free_struct_scoreboard(&out->struct_sb);
	cf_map8_free(&out->file_map);
	cf_map8_free(&out->type_map);
	clang_disposeIndex(out->clang_index);
	return error;
}

/*
 * Initialize db-related members of an `index_ctx_t`.
 */
static int
make_index_ctx_db(const index_config_t *config, index_ctx_t *out)
{
	int error;

	// specially handle a database is passed in via `config`
	if (config->db_kind == index_db_borrowed) {
		out->db = config->db_args.db;
		out->db_owned = false;
		return 0;
	}

	// database is owned by `*out`

	switch (config->db_kind) {
		case index_db_nop:
			error = cf_db_open_nop(&out->db_);
			break;
		case index_db_mem:
			error = cf_db_open_mem(&out->db_);
			break;
		case index_db_sql:
			error = cf_db_open_sql(config->db_args.sql_path, /*ro*/false,
					&out->db_);
			break;
		default:
			cf_assert(config->db_kind != index_db_borrowed);
			error = EINVAL;
			break;
	}

	if (error) {
		goto fail;
	}

	out->db = &out->db_;
	out->db_owned = true;

fail:
	return error;
}

/*
 * Free the internal resources of a `index_ctx_t` initialized from a previous
 * successful call to make_index_ctx().
 */
static void
free_index_ctx(index_ctx_t *ctx)
{
	cf_print_debug("free index_ctx %p: %zu files, %zu types\n",
			ctx, cf_map8_len(&ctx->file_map), cf_map8_len(&ctx->type_map));
	if (ctx->db_owned) {
		cf_db_close(&ctx->db_);
	}
	free_struct_scoreboard(&ctx->struct_sb);
	free_ast_path(&ctx->path);
	cf_map8_free(&ctx->file_map);
	cf_map8_free(&ctx->type_map);
	clang_disposeIndex(ctx->clang_index);
}

/*
 * Get rid of TU-specific state in `ctx`.
 *
 * The idea is that pointers into AST (like `clang::Type*`) aren't meaningful
 * between TUs.
 *
 * Reset the following members:
 * - type_map
 * - file_map
 */
static void
reset_tu_ctx(index_ctx_t *ctx)
{
	cf_map8_reset(&ctx->file_map);
	cf_map8_reset(&ctx->type_map);
}

static void
make_ast_path(ast_path_t *out)
{
	memset(out, 0, sizeof(*out));
	cursor_stack_make(&out->parent_stack);
}

static void
free_ast_path(ast_path_t *path)
{
	cursor_stack_free(&path->parent_stack);
}

static void
reset_ast_path(ast_path_t *path)
{
	cursor_stack_reset(&path->parent_stack);
	path->count = 0;
}
