/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Vector/dynamic array library.
 *
 * This is an alternative to other C preprocessor macro-based implementations.
 *
 * A minimal amount of C code is codegen'ed for each object type stored in a
 * vector. A single set of C functions is used, and the core struct has an
 * additional `stride` to make it generic for all types.
 */
#pragma once

#include "cc_support.h"

#include <stddef.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_DECLS

/*
 * Macro constants.
 *
 * - CF_VEC_DATA_ALIGN
 *   Required alignment of the heap buffer used for `cf_vec_t::packed_data`.
 * - CF_VEC_MASK
 *   Bit mask used to select the low bits of `cf_vec_t::packed_data`.
 *   This is suffixed `ul` to promote it to the same width as `uintptr_t`.
 * - CF_VEC_RESERVED
 *   Used to mark a vector is in the middle of changing any entry. This is set
 *   between cf_vec_reserve() and cf_vec_commit(), as well as for the _pop()
 *   functions.
 * - CF_VEC_ITERATED
 *   Used to mark a vector is being iterated over. This is set between
 *   cf_vec_iter_make() and cf_vec_iter_free().
 */
#define CF_VEC_DATA_ALIGN alignof(long)
#define CF_VEC_MASK 0x3ul
#define CF_VEC_RESERVED 0x1ul
#define CF_VEC_ITERATED 0x2ul
_Static_assert(CF_VEC_MASK < CF_VEC_DATA_ALIGN,
		"flag bits must fit into expected alignment from malloc");

/*
 * A generic vector data structure.
 *
 * This is different from a C++ template implementation in that the size of the
 * code generated is minimized. Instead of codegening a whole set of functions
 * per vector template-type, only a set of wrappers that just perform casts
 * are created. At the core, there is only one single struct and a set of C
 * functions used for all stored types. The struct has the overhead of
 * recording the type's size/alignment (the `stride` below) to make it work for
 * any type.
 *
 * Consider using a new type generated from CF_VEC_GENERATE() rather than
 * directly using this structure and the cf_* functions.
 *
 * Members
 * - packed_data
 *   Packed pointer to vector data. The low two bits (selected with
 *   CF_VEC_MASK) are used to track outstanding reservations or iterators.
 * - len
 *   Current logical size in *bytes*. This is always a multiple of `stride`.
 * - capacity
 *   Physical capacity in *bytes*. This is always a multiple of `stride`.
 * - stride
 *   Multiple, in bytes, at which objects are stored in `*data`. E.g.,
 *   - for `char [13]`, stride = 13
 *   - for `int32_t __attribute__((aligned(8)))`, stride = 8
 */
typedef struct {
	uintptr_t packed_data;
	size_t len;
	size_t capacity;
	size_t stride;
} cf_vec_t;

/*
 * Vector iterator.
 *
 * Example use:
 *   cf_vec_t vec = ...;
 *   cf_vec_iter_t it;
 *
 *   cf_vec_iter_make(&vec, &it);
 *   while (cf_vec_iter_next(&it)) {
 *     ... cf_vec_iter_peek(&it);
 *     // do something with the peeked value
 *   }
 *   cf_vec_iter_free(&it);
 *
 * Members
 * - parent
 *   Borrowed pointer to vector being iterated over.
 * - offset
 *   Byte offset into parent vector's `data` member. A value `SIZE_MAX` is used
 *   to signal that the iterator has been initialized, but cf_vec_iter_next()
 *   hasn't been called for the first time yet.
 */
typedef struct {
	cf_vec_t *parent;
	size_t offset;
} cf_vec_iter_t;

void cf_vec_make(size_t type_size, size_t type_align, cf_vec_t *out);
void cf_vec_free(cf_vec_t *vec);
void cf_vec_reset(cf_vec_t *vec);
void *cf_vec_detach(cf_vec_t *vec);

bool cf_vec_push(cf_vec_t *vec, const void *new_entry, size_t size);
void *cf_vec_reserve(cf_vec_t *vec);
void cf_vec_commit(cf_vec_t *vec, void *reservation);
void cf_vec_abort(cf_vec_t *vec, void *reservation);
void cf_vec_remove(cf_vec_t *vec, void *entry);

void *cf_vec_pop_start(cf_vec_t *vec);
void cf_vec_pop_end(cf_vec_t *vec, void *pop_value);

void *cf_vec_at(const cf_vec_t *vec, size_t i);
size_t cf_vec_len(const cf_vec_t *vec);

void cf_vec_iter_make(cf_vec_t *vec, cf_vec_iter_t *out);
void cf_vec_iter_free(cf_vec_iter_t *it);
void *cf_vec_iter_peek(const cf_vec_iter_t *it);
bool cf_vec_iter_next(cf_vec_iter_t *it);

/*
 * Codegen wrappers for a type-safe vector of `type`.
 *
 * Two things are emitted:
 * - a new struct named `vec_name`
 *   Note: this is the typedef name, not the struct tag
 * - a set of functions
 *   Each is a wrapper to do nothing more than type check for `vec_name` and
 *   `type`.
 *
 * Each emitted function is `static inline` prefixed with a name specified by
 * `prefix`. E.g.,
 *   `CF_VEC_GENERATE(foo_vec_t, foo_t, foo_vec)`
 * will generate:
 * - void foo_vec_make(foo_vec_t *);
 * - void foo_vec_free(foo_vec_t *);
 * - void foo_vec_reset(foo_vec_t *);
 * - foo_t *foo_vec_detach(foo_vec_t *);
 * - bool foo_vec_push(foo_vec_t *, const foo_t *new_entry);
 * - foo_t *foo_vec_reserve(foo_vec_t *);
 * - void foo_vec_commit(foo_vec_t *, foo_t *reservation);
 * - void foo_vec_abort(foo_vec_t *, foo_t *reservation);
 * - foo_t *foo_vec_pop_start(foo_vec_t *);
 * - void foo_vec_pop_end(foo_vec_t *, foo_t *pop_value);
 * - foo_t *foo_vec_at(const foo_vec_t *, ...);
 * - size_t foo_vec_len(const foo_vec_t *);
 */
#define CF_VEC_GENERATE(vec_name, type, prefix) \
	CF_VEC_TYPE_DECL(vec_name, type) \
	CF_VEC_FUNC_DECL(vec_name, type, prefix)

#define CF_VEC_TYPE_DECL(vec_name, type) \
	_Static_assert(sizeof(type), "vector only supports sized types"); \
	_Static_assert(alignof(type) && (alignof(type) <= CF_VEC_DATA_ALIGN), \
			"bad vector type alignment"); \
	typedef struct { \
		cf_vec_t v; \
	} vec_name;

#define CF_VEC_FUNC_DECL(vec_name, type, prefix) \
	_Static_assert(sizeof(type), "vector only supports sized types"); \
	_Static_assert(alignof(type) && (alignof(type) <= CF_VEC_DATA_ALIGN), \
			"bad vector type alignment"); \
	__attribute__((unused)) static inline void \
	prefix ## _make(vec_name *out) { \
		return cf_vec_make(sizeof(type), alignof(type), &out->v); \
	} \
	__attribute__((unused)) static inline void \
	prefix ## _free(vec_name *vec) { \
		return cf_vec_free(&vec->v); \
	} \
	__attribute__((unused)) static inline void \
	prefix ## _reset(vec_name *vec) { \
		return cf_vec_reset(&vec->v); \
	} \
	__attribute__((unused)) static inline type * \
	prefix ## _detach(vec_name *vec) { \
		return cf_vec_detach(&vec->v); \
	} \
	__attribute__((unused)) static inline bool \
	prefix ## _push(vec_name *vec, const type *new_entry) { \
		return cf_vec_push(&vec->v, new_entry, sizeof(type)); \
	} \
	__attribute__((unused)) static inline type * \
	prefix ## _reserve(vec_name *vec) { \
		return cf_vec_reserve(&vec->v); \
	} \
	__attribute__((unused)) static inline void \
	prefix ## _commit(vec_name *vec, type *reservation) { \
		return cf_vec_commit(&vec->v, reservation); \
	} \
	__attribute__((unused)) static inline void \
	prefix ## _abort(vec_name *vec, type *reservation) { \
		return cf_vec_abort(&vec->v, reservation); \
	} \
	__attribute__((unused)) static inline type * \
	prefix ## _pop_start(vec_name *vec) { \
		return cf_vec_pop_start(&vec->v); \
	} \
	__attribute__((unused)) static inline void \
	prefix ## _pop_end(vec_name *vec, type *pop_value) { \
		return cf_vec_pop_end(&vec->v, pop_value); \
	} \
	__attribute__((unused)) static inline type * \
	prefix ## _at(const vec_name *vec, size_t i) { \
		return cf_vec_at(&vec->v, i); \
	} \
	__attribute__((unused)) static inline size_t \
	prefix ## _len(const vec_name *vec) { \
		return cf_vec_len(&vec->v); \
	} \

/*
 * Codegen wrappers to cf_vec_iter_* functions for a vector of `type`.
 *
 * Similar to CF_VEC_GENERATE(). E.g.,
 * `CF_VEC_ITER_GENERATE(foo_vec_t, foo_t, foo_iter)` will generate:
 * - void foo_iter_make(foo_vec_t *, ...);
 * - void foo_iter_free(...);
 * - foo_t *foo_iter_peek(...);
 * - bool foo_iter_next(...);
 *
 * Separate from CF_VEC_GENERATE() because not all vectors need iterator
 * functions generated.
 */
#define CF_VEC_ITER_GENERATE(vec_name, type, prefix) \
	CF_VEC_ITER_GENERATE_(vec_name, type, prefix)
#define CF_VEC_ITER_GENERATE_(vec_name, type, prefix) \
	static inline void \
	prefix ## _make(vec_name *vec, cf_vec_iter_t *out) { \
		return cf_vec_iter_make(&vec->v, out); \
	} \
	static inline void \
	prefix ## _free(cf_vec_iter_t *it) { \
		return cf_vec_iter_free(it); \
	} \
	static inline type * \
	prefix ## _peek(const cf_vec_iter_t *it) { \
		return cf_vec_iter_peek(it); \
	} \
	static inline bool \
	prefix ## _next(cf_vec_iter_t *it) { \
		return cf_vec_iter_next(it); \
	} \

__END_DECLS
