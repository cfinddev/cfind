/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * sqlite3 database schema as C preprocessor macros.
 */
#pragma once

/*
 * Table descriptions:
 *
 * - file
 *   Central table for all C source-containing files indexed by cfind. All
 *   other tables that contain a source code location reference a row in the
 *   file table by rowid.
 * - type
 *   Central table for all user-defined types (structs, unions, enums).
 *   All other tables that record something about the use of a type reference
 *   a row in the type table by rowid. Note that the primary name of a type
 *   (e.g. `struct foo`) is *not* stored in this table. All names of a type are
 *   represented as different rows in the typename table.
 * - typename
 *   A table of keys into the type table. Each entry references a row in the
 *   type table (in a many-to-one relationship), and specifies the kind of name
 *   created for the type: primary type name, typedef, instance variable.
 * - incomplete-type
 *   An internal-only table used to deal with incomplete types/forward
 *   declarations that are encountered before the definition of a type.
 */

#define FILE_TABLE_NAME "file_table"
#define FILE_COLUMN_NAMES "id, path"
#define FILE_COLUMNS "(" \
	"id INTEGER PRIMARY KEY ASC," \
	"path STRING" \
	")"
#define FILE_NUM_COLUMNS 2

#define TYPE_TABLE_NAME "type_table"
#define TYPE_COLUMN_NAMES \
	"typeid, kind, complete, file, func, scope, line, column"
#define TYPE_COLUMNS "(" \
	"typeid INTEGER PRIMARY KEY ASC," \
	"kind INT," \
	"complete INT," \
	"file INT," \
	"func INT," \
	"scope INT," \
	"line INT," \
	"column INT" \
	")"
#define TYPE_NUM_COLUMNS 8

#define TYPENAME_TABLE_NAME "typename"
#define TYPENAME_COLUMN_NAMES \
	"name, kind, base_type, file, func, scope, line, column"
#define TYPENAME_COLUMNS "(" \
	"name STRING," /*NOTE: not unique*/ \
	"kind INT," \
	"base_type INT," \
	"file INT," \
	"func INT," \
	"scope INT," \
	"line INT," \
	"column INT" \
	")"
#define TYPENAME_NUM_COLUMNS 8

#define INCOMPLETE_TYPE_TABLE_NAME "incomplete_type"
#define INCOMPLETE_TYPE_COLUMN_NAMES \
	"(name, kind, base_type, file, line, column)"
/*
 * Note: no 'func' or 'scope'. Function-scope incomplete types aren't worth
 * indexing because they're always completed within the same function.
 */
#define INCOMPLETE_TYPE_COLUMNS "(" \
	"name STRING," \
	"kind INT," \
	"base_type INT," \
	"file INT," \
	"line INT," \
	"column INT" \
	")"
#define INCOMPLETE_TYPE_NUM_COLUMNS 6

#define TYPE_USE_TABLE_NAME "type_use"
#define TYPE_USE_COLUMN_NAMES "base_type, kind, file, line, column"
#define TYPE_USE_COLUMNS "(" \
	"base_type INT," \
	"kind INT," \
	"file INT," \
	"line INT," \
	"column INT" \
	")"
#define TYPE_USE_NUM_COLUMNS 5

#define MEMBER_TABLE_NAME "members"
#define MEMBER_COLUMN_NAMES "parent, base_type, name, file, line, column"
#define MEMBER_COLUMNS "(" \
	"parent INT," \
	"base_type INT," \
	"name STRING," \
	"file INT," \
	"line INT," \
	"column INT" \
	")"
#define MEMBER_NUM_COLUMNS 6
