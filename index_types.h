/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#pragma once

#include "cf_db.h"
#include "cf_map.h"
#include "cf_vector.h"
#include "db_types.h"

#include <stdbool.h>
#include <stdint.h>

#include "clang-c/Index.h"

CF_VEC_TYPE_DECL(cursor_stack_t, CXCursor);

/*
 * Track position in AST.
 *
 * Uses?:
 * - pretty printing
 * - track the whole path
 *
 * Members
 * - parent_stack
 *   Path of parent nodes down to the current position of the cursor.
 * - count
 *   Total number of nodes seen.
 */
typedef struct {
	cursor_stack_t parent_stack;
	uint32_t count;
} ast_path_t;

/*
 * All database entries for a struct/union/enum glued together.
 *
 * This is used as the vector entry type in `struct_scoreboard_t::new_types`.
 * Members:
 * - type_id
 *   Unique `clang::Type*` for record decl.
 * - entry
 *   Database entry for the struct itself.
 * - name
 *   Optional typename. The scoreboard tracks, out of band, whether this member
 *   is initialized.
 * - loc
 *   Source locations.
 *   - [0] for `entry`
 *   - [1] optionally for `name`
 */
typedef struct {
	clang_type_t type_id;
	db_type_entry_t entry;
	db_typename_t name; // optional
	loc_ctx_t loc[2];
} struct_pkg_t;

/*
 * Glued together database entries for a struct/union member.
 */
typedef struct {
	clang_type_t parent;
	db_member_t entry;
	loc_ctx_t loc;
} member_pkg_t;

/*
 * Glued together database entries for a type use.
 *
 * Note: `where` is needed to avoid duplicated type uses when a structure is
 * reparsed.
 *
 * struct foo {
 *   struct bar *b;
 * };
 *
 * Should only ever emit 1 `struct bar` usage even if its header file is
 * indexed multiple times.
 */
typedef struct {
	clang_type_t where;
	db_type_use_t entry;
	loc_ctx_t loc;
} type_use_pkg_t;

CF_VEC_TYPE_DECL(struct_vec_t, struct_pkg_t);
CF_VEC_TYPE_DECL(memberpkg_vec_t, member_pkg_t);
CF_VEC_TYPE_DECL(typeusepkg_vec_t, type_use_pkg_t);

/*
 * State built up while traversing a struct/union/enum.
 *
 * Unlike other entries, C record types cannot simply be inserted into
 * the database. struct/unions and their children need to be conditionally
 * inserted into the database. The sub-AST beneath a record decl is converted
 * into a set of in-memory database entries. The whole set is then 
 * committed in pieces. `struct_scoreboard_t` is used for this purpose. See
 * index_struct() for the motivation.
 *
 * Members
 * - path
 * - current_parent_stack
 *   Used for indexing anonymous types. The members of anonymous types are
 *   added as children of the most recent named parent.
 * - loc
 *   Current source location.
 * - new_types
 * - members
 * - type_uses
 * - unnamed_types
 */
typedef struct {
	ast_path_t path;
	cursor_stack_t current_parent_stack;
	loc_ctx_t loc;

	struct_vec_t new_types;
	memberpkg_vec_t members;
	typeusepkg_vec_t type_uses;
	cf_map8_t unnamed_types;
} struct_scoreboard_t;

/*
 * Indexing context.
 *
 * State tracked when indexing an AST. Most members are specific to a TU.
 * reset_tu_ctx() is called to reset state between TUs.
 *
 * Members
 * - clang_index
 *   clang collection of TUs parsed from a compilation database -- not to be
 *   confused with cfind's index.
 * - db_
 *   Optional storage for an owned database. This is only used if `db_owned` is
 *   true, otherwise it's uninitialized.
 * - db
 *   The persistent database. This stores entries for indexed nodes. It either
 *   points to `db_` or it borrows from a caller-provided database passed in
 *   from cf_index_project().
 * - db_owned
 *   True if the database `db` points to is owned. In which case, the database
 *   is freed when the `index_ctx_t` is.
 * - file_map
 *   Map from opaque clang `CXFile` pointer to database `file_ref_t`. This is
 *   used to identify the file the source for an AST node appears in.
 * - type_map
 *   Map from opaque `clang::Type*` to database `type_ref_t`. This is used to
 *   identify types that have already been inserted into the database, as well
 *   as to create database entries from AST nodes that reference a type.
 * - path
 *   Stack data structure used to track the position in the AST.
 * - loc
 *   The source location of the current AST node.
 * - struct_sb
 *   State maintained while traversing a struct/union/enum type declaration.
 * - last_struct
 *   The `clang::Type*` of the last struct indexed. This is only used to assign
 *   names to top-level unnamed structs (i.e., for `typedef struct {} foo_t;`).
 */
typedef struct {
	CXIndex clang_index;
	cf_db_t db_;
	cf_db_t *db;
	bool db_owned;

	cf_map8_t file_map;
	cf_map8_t type_map;
	ast_path_t path;
	loc_ctx_t loc;
	struct_scoreboard_t struct_sb;

	clang_type_t last_struct;
} index_ctx_t;
