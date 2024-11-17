/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Query descriptions.
 *
 * Each describes a sql query in terms of its sql language statement and types
 * of input and output columns. This file is similar to "sql_schema.h". The
 * goal is to not overcrowd "sql_query.c" but rather than be a normal header
 * file included in many places.
 *
 * The point of centralizing all queries here is to make is easy to find any
 * sql language string that gets passed into sqlite. It is *not* to make
 * changes easy.
 *
 * Modifying one of the `query_desc_t` below requires updates to bind and exec
 * functions in "sql_query.c". This is because they hard code input and output
 * indices.
 */
#ifdef _QUERY_DESC_H_
#error "query_desc.h" can only be included once
#else
#define _QUERY_DESC_H_
#endif // _QUERY_DESC_H_

#include "sql_types.h"
#include "sql_schema.h"

/*
 * Start and end pointers to query description section.
 *
 * Type `char[]` because it holds `lookup_desc_t` and `query_desc_t`.
 */
extern const char query_section_start[] QUERY_SECTION_START;
extern const char query_section_end[] QUERY_SECTION_STOP;

static const QUERY_ATTR lookup_desc_t file_lookup_query = {
	.base = {
		.query = "SELECT " \
				"id " \
				"FROM " FILE_TABLE_NAME " WHERE (" \
				"(path == ?1)" \
				");",
		.num_columns = 1,
		.column_kinds = (const column_kind_t[]) {
			[0] = column_str,
		},
	},
	.num_outputs = 1,
	.output_kinds = (const column_kind_t[]) {
		[0] = column_uint64,
	},
};

static const QUERY_ATTR lookup_desc_t file_id_lookup_query = {
	.base = {
		.query = "SELECT " \
				"path " \
				"FROM " FILE_TABLE_NAME " WHERE (" \
				"(id == ?1)" \
				");",
		.num_columns = 1,
		.column_kinds = (const column_kind_t[]) {
			[0] = column_uint64,
		},
	},
	.num_outputs = 1,
	.output_kinds = (const column_kind_t[]) {
		[0] = column_str,
	},
};

static const QUERY_ATTR query_desc_t file_insert_query = {
	.query = "INSERT INTO " \
			FILE_TABLE_NAME " " \
			"(" FILE_COLUMN_NAMES ") " \
			"VALUES (?1, ?2);",
	.num_columns = 2,
	.column_kinds = (const column_kind_t[]) {
		[0] = column_null,
		[1] = column_str,
	},
};

static const QUERY_ATTR lookup_desc_t type_lookup_query = {
	.base = {
		.query = "SELECT " \
				TYPE_COLUMN_NAMES " " \
				"FROM " TYPE_TABLE_NAME " WHERE " \
				"(typeid == ?1);",
		.num_columns = 1,
		.column_kinds = (const column_kind_t[]) {
			[0] = column_uint64,
		},
	},
	.num_outputs = 8,
	/*
	 * this should just be a property of the table instead of each individual
	 * query
	 */
	.output_kinds = (const column_kind_t[]) {
		[0] = column_uint64,
		[1] = column_uint32,
		[2] = column_uint32,
		[3] = column_uint64,
		[4] = column_uint64,
		[5] = column_uint32,
		[6] = column_uint32,
		[7] = column_uint32,
	},
};

static const QUERY_ATTR query_desc_t type_insert_query = {
	.query = "INSERT INTO " \
			TYPE_TABLE_NAME " " \
			"(" TYPE_COLUMN_NAMES ") " \
			"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);",
	.num_columns = 8,
	.column_kinds = (const column_kind_t[]) {
		[0] = column_null,
		[1] = column_uint32,
		[2] = column_uint32,
		[3] = column_uint64,
		[4] = column_uint64,
		[5] = column_uint32,
		[6] = column_uint32,
		[7] = column_uint32,
	},
};

static const QUERY_ATTR lookup_desc_t typename_lookup_query = {
	.base = {
		// XXX hard coded for global scope lookups
		.query = "SELECT " \
				"base_type,kind " \
				"FROM " TYPENAME_TABLE_NAME " WHERE (" \
				"(file == ?1) AND " \
				"(name == ?2) AND " \
				"(scope == 0) " \
				");",
		.num_columns = 2,
		.column_kinds = (const column_kind_t[]) {
			[0] = column_uint64,
			[1] = column_str,
		},
	},
	.num_outputs = 2,
	.output_kinds = (const column_kind_t[]) {
		[0] = column_uint64,
		[1] = column_uint32,
	},
};

static const QUERY_ATTR lookup_desc_t typename_find_query = {
	.base = {
		// XXX hard coded for global scope lookups
		.query = "SELECT " \
				TYPENAME_COLUMN_NAMES \
				" FROM " TYPENAME_TABLE_NAME " WHERE (" \
				"(name LIKE ?1)" \
				");",
		.num_columns = 1,
		.column_kinds = (const column_kind_t[]) {
			[0] = column_str,
		},
	},
	.num_outputs = 8,
	.output_kinds = (const column_kind_t[]) {
		[0] = column_str,
		[1] = column_uint32,
		[2] = column_uint64,
		[3] = column_uint64,
		[4] = column_uint64,
		[5] = column_uint32,
		[6] = column_uint32,
		[7] = column_uint32,
	},
};

static const QUERY_ATTR query_desc_t typename_insert_query = {
	.query = "INSERT INTO " \
			TYPENAME_TABLE_NAME " " \
			"(" TYPENAME_COLUMN_NAMES ") " \
			"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);",
	.num_columns = 8,
	.column_kinds = (const column_kind_t[]) {
		[0] = column_str,
		[1] = column_uint32,
		[2] = column_uint32,
		[3] = column_uint64,
		[4] = column_uint64,
		[5] = column_uint32,
		[6] = column_uint32,
		[7] = column_uint32,
	},
};

static const QUERY_ATTR query_desc_t type_use_insert_query = {
	.query = "INSERT INTO " \
			TYPE_USE_TABLE_NAME " " \
			"(" TYPE_USE_COLUMN_NAMES ") " \
			"VALUES (?1, ?2, ?3, ?4, ?5);",
	.num_columns = 5,
	.column_kinds = (const column_kind_t[]) {
		[0] = column_uint64,
		[1] = column_uint32,
		[2] = column_uint64,
		[3] = column_uint32,
		[4] = column_uint32,
	},
};

static const QUERY_ATTR query_desc_t member_insert_query = {
	.query = "INSERT INTO " \
			MEMBER_TABLE_NAME " " \
			"(" MEMBER_COLUMN_NAMES ") " \
			"VALUES (?1, ?2, ?3, ?4, ?5, ?6);",
	.num_columns = 6,
	.column_kinds = (const column_kind_t[]) {
		[0] = column_uint64,
		[1] = column_uint64,
		[2] = column_str,
		[3] = column_uint64,
		[4] = column_uint32,
		[5] = column_uint32,
	},
};

static const QUERY_ATTR lookup_desc_t member_lookup_query = {
	.base = {
		.query = "SELECT " \
				MEMBER_COLUMN_NAMES \
				" FROM " MEMBER_TABLE_NAME " WHERE (" \
				"(parent == ?1) AND" \
				"(name LIKE ?2)" \
				");",
		.num_columns = 2,
		.column_kinds = (const column_kind_t[]) {
			[0] = column_uint64,
			[1] = column_str,
		},
	},
	.num_outputs = 6,
	.output_kinds = (const column_kind_t[]) {
		[0] = column_uint64,
		[1] = column_uint64,
		[2] = column_str,
		[3] = column_uint64,
		[4] = column_uint32,
		[5] = column_uint32,
	},
};
