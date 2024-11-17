/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * sql types
 *
 * Various types that help in making sql queries.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Object file section in which all query descriptions are placed.
 */
#define QUERY_SECTION_NAME "cf_query"

/*
 * Attribute that places a `query_desc_t` variable into the query descriptions
 * section.
 */
#define QUERY_ATTR __attribute__((used, __section__(QUERY_SECTION_NAME)))

#define QUERY_SECTION_START __asm__("__start_" QUERY_SECTION_NAME)
#define QUERY_SECTION_STOP __asm__("__stop_" QUERY_SECTION_NAME)

/*
 * cfind only supports a subset of sqlite data types.
 *
 * In other words, float and non-utf8 strings aren't useful.
 */
typedef enum {
	column_null,
	column_uint32,
	column_uint64,
	column_str,
} column_kind_t;

/*
 * Types are chosen according to cfind's usage. sqlite stores signed integers
 * on disk.
 *
 * Integers are converted to/from sqlite's integer types from a `serial_row_t`
 * is converted to/from a `sqlite3_stmt`.
 *
 * `column_values` two pointers (16B) large. Not very padding efficient in an
 * array but easy to read and write.
 */
typedef union {
	bool null_val;
	uint32_t uint32_val;
	uint64_t uint64_val;
	cf_str_t str_val;
} column_val_t;

/*
 * Intermediate representation of a row.
 *
 * Serialize structs then bind the row to a sql statement. This separates
 * serialization (manual boilerplate) and sqlite calls (lots of error
 * checking).
 *
 * insert does:
 *   struct -> serial_row_t -> sqlite3_stmt
 * lookup does
 *   sqlite3_stmt -> serial_row_t -> struct
 */
typedef struct {
	size_t num_columns;
	const column_kind_t *column_kinds;
	column_val_t *column_values;
} serial_row_t;

/*
 * Query description.
 *
 * Members
 * - query
 *   The sqlite3 query itself in string form.
 * - num_columns
 *   Length of `column_kinds`.
 * - column_kinds
 *   An array that describes the types of each column variable in `query`.
 *   I.e., each "?N" placeholder has an entry at `column_kinds[N-1]`.
 */
typedef struct {
	const char *query;
	size_t num_columns;
	const column_kind_t *column_kinds;
} query_desc_t;

/*
 * Lookup description.
 *
 * A query description with extra output column info.
 *
 * Members
 * - base
 *   The base part of the query. `base.column_kinds` is used for the "WHERE"
 *   variables.
 * - num_outputs
 *   Length of `output_kinds`.
 * - output_kinds
 *   An array that describes the types of the output columns of `base`. I.e.,
 *   each names in "SELECT a,b" has an entry in `output_kinds` in the order
 *   they appear.
 */
typedef struct {
	query_desc_t base;
	size_t num_outputs;
	const column_kind_t *output_kinds;
} lookup_desc_t;
