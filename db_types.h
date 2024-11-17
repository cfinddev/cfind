/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Types for entries stored in the database.
 *
 * The database frontend creates instances of these types and hands them to the
 * database backend to store.
 */
#pragma once

#include "cc_support.h"
#include "cf_string.h"
#include "cf_vector.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

__BEGIN_DECLS

/*
 * Opaque `clang::Type *`.
 *
 * cfind uses this as a unique identifier for a type within a translation unit.
 */
typedef void *clang_type_t;

/*
 * Reference to a file entry in a database.
 *
 * Opaque to the database frontend, the union variant is determined by the
 * backend.
 */
typedef union {
	int64_t rowid;
	size_t index;
} file_ref_t;

/*
 * Reference to a type entry in a database.
 *
 * A persistent unique identifier for a `db_type_entry_t`. Other database
 * entries (`db_typename_t`) use this to "point" to a particular type entry.
 *
 * Like `file_ref_t`, the union variant is determined by the database backend.
 */
typedef union {
	int64_t rowid;
	size_t index;
	clang_type_t p;
} type_ref_t;

/*
 * Reference to a function entry in a database.
 *
 * XXX currently unused
 */
typedef union {
	int64_t rowid;
	size_t index;
} func_ref_t;

/*
 * Full context to describe the source location of any db entry.
 *
 * This is detached from most other database structs (e.g., `db_typename_t`).
 *
 * Members
 * - file
 *   Reference to containing file.
 * - func
 *   Reference to containing function. Records at global scope use value 0
 *   (according to the active union variant).
 * - scope
 *   A value to uniqueify declarations that have the same name but appear at
 *   different scopes. It's more or less the number of unpaired `{`s before a
 *   declaration. See `decl_scope_t` for values.
 * - line
 *   Source line. Starts from value 1.
 * - column
 *   Source column. Starts from value 1.
 *
 * For trivial, non-definition entries like `db_member_use_t` members like
 * `func` and `scope` aren't serialized to disk.
 */
typedef struct {
	file_ref_t file;
	func_ref_t func;
	uint32_t scope;
	uint32_t line;
	uint32_t column;
} loc_ctx_t;

/*
 * Constants for `loc_ctx_t::scope`.
 *
 * `scope_nested` is the first nested scope value. A definition nested within a
 * function can have a `scope` with values in [2, UINT32_MAX].
 */
typedef enum {
	scope_global = 0,
	scope_func = 1,
	scope_nested = 2,
} decl_scope_t;

/*
 * C language kind of a user defined type.
 *
 * See `db_type_entry_t::kind`.
 */
typedef enum {
	type_kind_struct = 1,
	type_kind_union = 2,
	type_kind_enum = 3,
} type_kind_t;

/*
 * Database entry for a user defined type declaration.
 *
 * Members
 * - kind
 *   C language kind of this type (struct, union, enum).
 *   Note: typedefs are excluded.
 * - complete
 *   Whether this entry tracks a complete type definition.
 *   If false, the type must be completed somewhere else -- usually in a
 *   different ".c" file. XXX unused
 *
 * Note: this structure contains no name member because not every type (i.e.
 * an unnamed type) has a direct name. Each type name that could be used to
 * refer to a type is represented separately as a `db_typename_t`.
 */
typedef struct {
	type_kind_t kind;
	bool complete;
} db_type_entry_t;

/*
 * The different variants of a `db_typename_t`.
 *
 * Enumerators:
 * - direct
 *   The common case of a name directly defined with a type.
 *   "foo" in `struct foo {};`
 * - typedef
 *   Any name defined by a typedef.
 *   "foo" in `typedef struct {} foo`;
 *   "foo_t" in `typedef struct foo foo_t;`
 * - var
 *   The name of an instance variable that serves as the only identifier for an
 *   anonymous type.
 *   "foo" in `struct {} foo;`
 *   Note: `struct foo {} f;` would only use "struct foo" as a typename.
 */
typedef enum {
	name_kind_direct = 1,
	name_kind_typedef = 2,
	name_kind_var = 3,
} typename_kind_t;

/*
 * Database entry for a name of a type.
 *
 * A typename serves to expand the set of names for a particular type.
 *
 * Members
 * - kind
 *   Variant of typename.
 * - base_type
 *   Database reference to `db_type_entry_t` whose name is described by this
 *   structure.
 * - name
 *   The identifier string itself.
 *   Note: for elaborated types, such as `struct foo;`, the name is only "foo".
 */
typedef struct {
	typename_kind_t kind;
	type_ref_t base_type;
	cf_str_t name;
} db_typename_t;

/*
 * Variable decl.
 *
 * Members
 * - parent
 *   Function in which the variable is declared. Value 0 for global scope.
 * - base_type
 *   Reference to type of the variable.
 * - name
 *   The identifier string.
 *
 * Use for regular variables only.
 */
typedef struct {
	func_ref_t parent;
	type_ref_t base_type;
	cf_str_t name;
} db_var_t;

/*
 * Member variable decl.
 *
 * Similar to `db_var_t` except that `parent` is a `type_ref_t`.
 *
 * Members
 * - parent
 *   struct/union in which the member is declared.
 * - base_type
 *   Type of the variable.
 * - name
 *   The identifier string.
 */
typedef struct {
	type_ref_t parent;
	type_ref_t base_type;
	cf_str_t name;
} db_member_t;

/*
 * Manner in which a struct/union member is used.
 *
 * See `db_member_use_t::kind`.
 *
 * Enumerators
 * - member_access_load
 *   A member is read from.
 *   E.g., `return f->a;`.
 * - member_access_store 
 *   Member is written to.
 *   E.g., `f->a = 0;`.
 * - member_access_rmw
 *   Member is both read and written to in a single operation.
 *   E.g., `f->a *= 2;`.
 * - member_access_loc
 *   The address to the member is formed.
 *   E.g., `memset(&f->a, ...);`.
 */
typedef enum {
	member_access_load = 1,
	member_access_store = 2,
	member_access_rmw = 3,
	member_access_loc = 4,
} member_access_kind_t;

/*
 * Database entry for a struct/union member access.
 *
 * XXX unused
 *
 * Note: this only tracks that the member of some type is accessed at some
 * location. It does *not* track the variables involved. More specifically,
 * this record tracks that, e.g., `foo_t::a` is written to. It does *not* track
 * that a particular variable, `f` of type `foo_t *`, has its member accessed
 * `f->a`.
 *
 * Members
 * - lhs
 *   "Left hand side". The struct/union being accessed.
 * - rhs
 *   "Right hand side". The name of the member being accessed.
 * - kind
 *   Classification of the type of member access.
 *   A load, a store, a read-modify-write, or use of the location of the member
 *   (e.g., `&f->a`).
 */
typedef struct {
	type_ref_t lhs;
	cf_str_t rhs;
	member_access_kind_t kind;
} db_member_use_t;

/*
 * Manner in which a type is used.
 *
 * See `db_type_use_t::kind`.
 *
 * Enumerators:
 * - use_decl
 *   A variable/member declaration of this type.
 * - use_init
 *   An initialization.
 * - use_param
 *   A function parameter.
 * - use_cast
 *   Any implicit or explicit cast.
 * - use_sizeof
 *   Builtin metaprogramming uses.
 *   `sizeof(T)`, `alignof(T)`, `_Generic(...) T:`
 */
typedef enum {
	type_use_decl = 1,
	type_use_init = 2,
	type_use_param = 3,
	type_use_cast = 4,
	type_use_sizeof = 5,
} type_use_kind_t;

/*
 * Miscellaneous use of a type.
 *
 * Members
 * - base_type
 *   Reference to type involved.
 * - use
 *   Further classification of the type usage.
 */
typedef struct {
	type_ref_t base_type;
	type_use_kind_t kind;
} db_type_use_t;

const char *db_type_kind_str(type_kind_t kind);
const char *db_member_access_str(member_access_kind_t kind);
const char *db_type_use_str(type_use_kind_t kind);

__END_DECLS
