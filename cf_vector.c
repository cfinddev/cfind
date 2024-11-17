/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "cf_vector.h"

#include "cc_support.h"
#include "cf_alloc.h"
#include "cf_assert.h"
#include "cf_print.h"

#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/param.h>

/*
 * Set this to `1` to enable logging of vector lifetime-related calls.
 */
#define CF_VECTOR_DEBUG 0

#if CF_VECTOR_DEBUG
#define cf_print_vec(fmt, ...) cf_print_debug(fmt, ##__VA_ARGS__)
#else
#define cf_print_vec(fmt, ...)
#endif // CF_VECTOR_DEBUG

/*
 * Value used to signal, in band, in `cf_vec_iter::offset` that an iterator is
 * in the state between cf_vec_iter_make() and the first cf_vec_iter_next()
 * call.
 */
#define ITER_OFFSET_UNSTARTED (SIZE_MAX)

static int resize_vec(cf_vec_t *vec, size_t new_capacity);
static bool grow_strategy(size_t old_capacity, size_t stride,
		size_t *capacity_out);

static void *vec_offset(uintptr_t packed_data, size_t offset);
static void *vec_data(uintptr_t packed_data);
static uintptr_t vec_bits(uintptr_t packed_data);

static void vec_set_bit(uintptr_t *packed_data, uintptr_t bit);
static void vec_clear_bit(uintptr_t *packed_data, uintptr_t bit);

static void assert_vec(const cf_vec_t *vec);
static void assert_vec_iter(const cf_vec_iter_t *it);

/*
 * API
 */

/*
 * Initialize `*out` to a new vector of objects each of `type_size` bytes large
 * and aligned to `type_align`.
 *
 * Free `out` with a future call to cf_vec_free().
 *
 * This function cannot fail. Allocation is deferred to the first insertion.
 */
void
cf_vec_make(size_t type_size, size_t type_align, cf_vec_t *out)
{
	cf_assert(type_size);
	cf_assert(type_align && (type_align <= CF_VEC_DATA_ALIGN));

	*out = (cf_vec_t){
		.stride = roundup(type_size, type_align),
	};
	cf_print_vec("new vec %p (align=%zu, size=%zu, stride=%zu)\n",
			out, type_align, type_size, out->stride);
	assert_vec(out);
}

/*
 * Free a vector initialized from a previous cf_vec_make() call.
 *
 * The heap allocation `vec` owns is freed. If the object type stored owns
 * external resources (e.g., a vector of file descriptors) they need to be
 * manually freed before calling this function.
 */
void
cf_vec_free(cf_vec_t *vec)
{
	cf_print_vec("free vec %p\n", vec);
	assert_vec(vec);
	// check no reservations/iterators outstanding
	cf_assert(!vec_bits(vec->packed_data));
	cf_free(vec_data(vec->packed_data));
}

/*
 * Reset `vec`s logical size yet retain its heap allocation.
 *
 * An atypical vector function useful when using a vector in a loop. The
 * following snippet:
 *   cf_vec_t vec;
 *   while(...) {
 *     cf_vec_make(..., &vec);
 *     ... use `vec` ...
 *     cf_vec_free(&vec);
 *   }
 * can instead be:
 *   cf_vec_t vec;
 *   cf_vec_make(..., &vec);
 *   while(...) {
 *     ... use `vec` ...
 *     cf_vec_reset(&vec);
 *   }
 *   cf_vec_free(&vec);
 *
 * The benefit of using reset is that it can reduce the number of
 * malloc()/free()s per the lifetime of the vector.
 *
 * Similar to cf_vec_free(), external resources need to be manually freed.
 */
void
cf_vec_reset(cf_vec_t *vec)
{
	cf_print_vec("reset vec %p\n", vec);
	cf_assert(vec);
	// check no reservations/iterators outstanding
	cf_assert(!vec_bits(vec->packed_data));
	vec->len = 0;
}

/*
 * Turn `vec` into an array, discarding the vector bits.
 *
 * End the lifetime of `vec`, but extend the lifetime of the array it
 * contained. This is useful for things that want to consume a more simple
 * array+length instead of the full vector structure.
 *
 * Example use:
 *   cf_vec_t vec;
 *   cf_vec_make(sizeof(int), alignof(int), &vec);
 *   ...
 *   const size_t len = cf_vec_len(&vec);
 *   int *const data = cf_vec_detach(&vec);
 *   ...
 *   cf_free(data);
 *
 * The return value needs to be cf_free()d. cf_vec_free() does *not* need to be
 * called.
 */
void *
cf_vec_detach(cf_vec_t *vec)
{
	cf_print_vec("detach vec %p\n", vec);
	assert_vec(vec);
	// check no reservations/iterators outstanding
	cf_assert(!vec_bits(vec->packed_data));

	return vec_data(vec->packed_data);
}

/*
 * Convenience function to _reserve(), memcpy(), _commit() `new_entry` to
 * `vec`.
 *
 * Note: it's up to the caller to determine whether the unerased type of
 * `new_entry` is trivially copyable.
 *
 * Return false if memory allocation fails.
 */
bool
cf_vec_push(cf_vec_t *vec, const void *new_entry, size_t size)
{
	// allocate a new entry at the end of `vec`
	void *reservation = cf_vec_reserve(vec);
	if (!reservation) {
		return false;
	}

	// copy `new_entry` in
	memcpy(reservation, new_entry, size);
	cf_vec_commit(vec, reservation);

	return true;
}

/*
 * Prepare `vec` for insertion of a new object.
 *
 * On success, a pointer to new space is returned. The caller must then
 * initialize the new object and then follow with a call to cf_vec_commit().
 * Example use:
 *   cf_vec_t vec = ...;
 *   float *new_entry = cf_vec_reserve(&vec);
 *   if (!new_entry) { ... give up ... }
 *   *new_entry = 123.4;
 *   cf_vec_commit(&vec, new_entry);
 *
 * Use of a cf_vec_reserve()/cf_vec_commit() pair is fairly restricted relative
 * to other vector operations (because the vector might resize). Notably,
 * iterator calls cannot be interleaved with insertion.
 *
 * cf_vec_abort() can be alternatively used to cancel the insertion.
 *
 * Steps:
 * - check `vec` isn't in the middle of another insert/pop/iteration
 * - resize if buffer is at capacity
 * - mark `vec` as being in middle of an insertion
 * - return a pointer to the logical end of the heap buffer
 *
 * This function can fail if the vector needs to resize itself. If allocation
 * fails, NULL is returned.
 */
void *
cf_vec_reserve(cf_vec_t *vec)
{
	assert_vec(vec);
	cf_assert(!vec_bits(vec->packed_data));

	// check if full; may need a resize
	if (vec->len == vec->capacity) {
		int error;
		size_t new_capacity;

		// compute new capacity
		if (!grow_strategy(vec->capacity, vec->stride, &new_capacity)) {
			cf_print_debug("cannot compute growth of cap=%zu, stride=%zu\n",
					vec->capacity, vec->stride);
			return NULL;
		}

		// do the resize
		if ((error = resize_vec(vec, new_capacity))) {
			cf_print_debug("cannot grow vector error=%d, cap=%zu\n",
					error, vec->capacity);
			return NULL;
		}
		cf_assert(vec->len < vec->capacity);
	}

	vec_set_bit(&vec->packed_data, CF_VEC_RESERVED);
	// return a pointer just past the last element
	return vec_offset(vec->packed_data, vec->len);
}

/*
 * Finish an insertion.
 *
 * Pass in `reservation` as the return value of a previous successful
 * cf_vec_reserve() call.
 *
 * Steps:
 * - update logical size to size of one new element
 *   This commits to the new entry. It can now be returned via cf_vec_at().
 * - clear out CF_VEC_RESERVED bit
 */
void
cf_vec_commit(cf_vec_t *vec, void *reservation)
{
	assert_vec(vec);
	cf_assert(vec_bits(vec->packed_data) == CF_VEC_RESERVED);
	cf_assert(reservation == vec_offset(vec->packed_data, vec->len));
	vec->len += vec->stride;
	vec_clear_bit(&vec->packed_data, CF_VEC_RESERVED);
}

/*
 * Cancel an insertion.
 *
 * The implementation is the same as commit() but without changing `len`.
 */
void
cf_vec_abort(cf_vec_t *vec, void *reservation)
{
	assert_vec(vec);
	cf_assert(vec_bits(vec->packed_data) == CF_VEC_RESERVED);
	cf_assert(reservation == vec_offset(vec->packed_data, vec->len));
	vec_clear_bit(&vec->packed_data, CF_VEC_RESERVED);
}

/*
 * Partially internal function. Remove `entry`.
 *
 * Remove `stride` number of bytes from `vec`. In diagram form, for a stride=4
 * and entry pointing to third element, go from
 *
 * 2222eeee4444...8888
 *     ^              ^
 *     entry          end
 *
 * to
 *
 * 22224444...8888____
 *     ^              ^
 *     entry          end
 *
 * Used only by cf_map8_remove(). No wrapper is emitted by CF_VEC_GENERATE().
 */
void
cf_vec_remove(cf_vec_t *vec, void *entry_)
{
	assert_vec(vec);
	// use `char *` so clang doesn't complain about `void *` arithmetic
	char *const entry = entry_;
	char *const data = vec_data(vec->packed_data);
	char *const end = (char *)data + vec->len;
	const size_t stride = vec->stride;

	// check `entry` points into the heap buffer
	cf_assert(data);
	cf_assert(vec->len >= stride);
	cf_assert(((uintptr_t)entry % stride) == 0);
	cf_assert((data <= entry) && (entry <= (end - stride)));

	void *const dst = entry;
	void *const src = entry + stride;
	const size_t remainder = (size_t)(end - entry) - stride;

	memmove(dst, src, remainder);
	vec->len -= stride;
}

/*
 * Inverse of cf_vec_reserve().
 *
 * Prepare to remove the last entry in `vec`, returning a pointer to it.
 * On success, the return value should be passed to cf_vec_pop_end() to
 * finalize the removal.
 *
 * Bit CF_VEC_RESERVED is reused to make sure a pop_start/pop_end pair doesn't
 * interleave with insertion or iteration.
 * Note: the vector isn't ever shrunk on removal.
 *
 * Return NULL if `vec` is empty.
 */
void *
cf_vec_pop_start(cf_vec_t *vec)
{
	assert_vec(vec);
	cf_assert(!vec_bits(vec->packed_data));

	if (!vec->len) {
		// empty; nothing to pop
		return NULL;
	}

	// compute the offset of the element to pop
	cf_assert(vec->len >= vec->stride);
	const size_t last_offset = vec->len - vec->stride;

	vec_set_bit(&vec->packed_data, CF_VEC_RESERVED);
	return vec_offset(vec->packed_data, last_offset);
}

/*
 * Commit to a pop() operation.
 *
 * The intended use is to have the caller directly free (or copy) and entry
 * before it is removed from the vector. Example use:
 *   cf_vec_t vec = ...;
 *   ...
 *   uint8_t **buf = cf_vec_pop_start(&vec);
 *   if (!buf) { ... vec is empty? ... }
 *   free(*buf);
 *   cf_vec_pop_end(&vec, buf);
 */
void
cf_vec_pop_end(cf_vec_t *vec, void *pop_value)
{
	assert_vec(vec);
	cf_assert(vec_bits(vec->packed_data) == CF_VEC_RESERVED);

	const size_t last_offset = vec->len - vec->stride;
	cf_assert(pop_value == vec_offset(vec->packed_data, last_offset));
	vec->len -= vec->stride;
	vec_clear_bit(&vec->packed_data, CF_VEC_RESERVED);
}

/*
 * Return the logical size of `vec` in number of elements.
 */
size_t
cf_vec_len(const cf_vec_t *vec)
{
	assert_vec(vec);
	return vec->len / vec->stride;
}

/*
 * Return a pointer to the element at index `i`.
 *
 * A weak pointer is returned; this borrows from `vec`. If `vec` is modified
 * by, e.g., cf_vec_push() or cf_vec_pop(), the pointer is invalidated. Further
 * access would be undefined behavior.
 *
 * Unlike cf_vec_reserve()/cf_vec_commit() there is no function to release the
 * return value. Furthermore, there are no assertions to check against unsafe
 * usage.
 */
void *
cf_vec_at(const cf_vec_t *vec, size_t i)
{
	size_t offset;
	if (__builtin_mul_overflow(i, vec->stride, &offset)) {
		cf_print_debug("index %zu too large to compute offset\n", i);
	}
	cf_assert(offset < vec->len);
	return vec_offset(vec->packed_data, offset);
}

/*
 * Initialize `out` to an iterator over all elements in `vec`.
 *
 * The iterator borrows from `vec` for its entire lifetime. Free `out` with a
 * call to cf_vec_iter_free().
 *
 * Limitations:
 * - a vector cannot be inserted into or removed from while being iterated
 * - a vector cannot have two iterators at once
 * - an iterator cannot be started while a vector is in the middle of insertion
 *   I.e.: reserve, iter_make, commit is prohibited
 */
void
cf_vec_iter_make(cf_vec_t *vec, cf_vec_iter_t *out)
{
	assert_vec(vec);
	cf_assert(!vec_bits(vec->packed_data));
	vec_set_bit(&vec->packed_data, CF_VEC_ITERATED);

	*out = (cf_vec_iter_t) {
		.parent = vec,
		.offset = ITER_OFFSET_UNSTARTED,
	};
	assert_vec_iter(out);
}

/*
 * Free an iterator initialized from a previous cf_vec_iter_make().
 */
void
cf_vec_iter_free(cf_vec_iter_t *it)
{
	assert_vec_iter(it);
	cf_assert(vec_bits(it->parent->packed_data) == CF_VEC_ITERATED);
	vec_clear_bit(&it->parent->packed_data, CF_VEC_ITERATED);
}

/*
 * Return a pointer to the element the iterator is currently on.
 */
void *
cf_vec_iter_peek(const cf_vec_iter_t *it)
{
	assert_vec_iter(it);
	// can't peek an iterator that hasn't been _next()ed once
	cf_assert(it->offset != ITER_OFFSET_UNSTARTED);

	return vec_offset(it->parent->packed_data, it->offset);
}

/*
 * Advance iterator `it`.
 *
 * If `offset` has value ITER_OFFSET_UNSTARTED, this is the first call to
 * _next().
 */
bool
cf_vec_iter_next(cf_vec_iter_t *it)
{
	if (it->offset == ITER_OFFSET_UNSTARTED) {
		it->offset = 0;
	} else {
		it->offset += it->parent->stride;
	}

	return it->offset < it->parent->len;
}

/*
 * Resize `vec` to a new physical size of `new_capacity` bytes.
 *
 * The old logical size is memcpy()ed, via cf_realloc(), to a new heap buffer.
 */
static int
resize_vec(cf_vec_t *vec, size_t new_capacity)
{
	cf_assert(vec->capacity < new_capacity);
	cf_assert((new_capacity % vec->stride) == 0);

	void *const new_data =
			cf_realloc(vec_data(vec->packed_data), new_capacity);
	if (!new_data) {
		return ENOMEM;
	}
	const uintptr_t new_packed_data = (uintptr_t)new_data;
	cf_assert((new_packed_data % CF_VEC_DATA_ALIGN) == 0);
	cf_assert(vec_bits(new_packed_data) == 0);

	vec->packed_data = new_packed_data;
	vec->capacity = new_capacity;
	return 0;
}

/*
 * Given a vector of `old_capacity` that needs to resize, compute its new
 * capacity.
 *
 * This implements linear growth. 8 elements are added on each resize.
 */
static bool
grow_strategy(size_t old_capacity, size_t stride, size_t *capacity_out)
{
	size_t new_capacity;
	// new_capacity = old_capacity + 8*stride
	if (__builtin_mul_overflow(stride, 8, &stride) ||
			__builtin_add_overflow(old_capacity, stride, &new_capacity)) {
		return false;
	}

	*capacity_out = new_capacity;
	return true;
}

/*
 * Given a packed pointer, return a pointer to `offset` bytes after.
 */
static void *
vec_offset(uintptr_t packed_data, size_t offset)
{
	return (char *)vec_data(packed_data) + offset;
}

/*
 * Return the pointer component of a packed pointer.
 */
static void *
vec_data(uintptr_t packed_data)
{
	return (void *)(packed_data & ~CF_VEC_MASK);
}

/*
 * Return just the flag bits of a packed pointer.
 */
static uintptr_t
vec_bits(uintptr_t packed_data)
{
	return packed_data & CF_VEC_MASK;
}

/*
 * Set a single bit value in a packed pointer.
 */
static void
vec_set_bit(uintptr_t *packed_data, uintptr_t bit)
{
	cf_assert(bit & CF_VEC_MASK);
	cf_assert(powerof2(bit));
	*packed_data |= bit;
}

/*
 * Clear a single bit value from a packed pointer.
 */
static void
vec_clear_bit(uintptr_t *packed_data, uintptr_t bit)
{
	cf_assert(bit & CF_VEC_MASK);
	cf_assert(powerof2(bit));
	*packed_data &= ~bit;
}

/*
 * Assert that `vec` is self consistent.
 *
 * A vector initialized by cf_vec_make() should always pass the following
 * checks:
 * - non-zero stride
 *   Only non-zero sized objects are stored.
 * - logical size never exceeds capacity
 *   Both are byte values and must always be multiples of `stride`.
 * - for an empty vector:
 *   Only the _ITERATED flag bit may be set.
 *   Length and capacity must be zero.
 * - the flag bits packed in `packed_data`
 *   A vector cannot be inserted into while being iterated over, and vice
 *   versa.
 */
static void
assert_vec(const cf_vec_t *vec)
{
	cf_assert(vec->stride);
	cf_assert(vec->len <= vec->capacity);
	cf_assert((vec->len % vec->stride) == 0);
	cf_assert((vec->capacity % vec->stride) == 0);

	const void *const data = vec_data(vec->packed_data);
	const uintptr_t bits = vec_bits(vec->packed_data);

	if (!data) {
		// a vector with NULL pointer cannot be in the middle of an insertion
		// (but an empty vector can be iterated over)
		cf_assert(!(bits & CF_VEC_RESERVED));

		// a NULL *packed* pointer must have zero length and capacity
		cf_assert(!vec->len);
		cf_assert(!vec->capacity);

		// skip further `data` checks
		return;
	}

	uintptr_t tmp;
	cf_assert(!__builtin_add_overflow((uintptr_t)data, vec->capacity, &tmp));
	cf_assert(((uintptr_t)data % CF_VEC_DATA_ALIGN) == 0);
	switch (bits) {
		case CF_VEC_RESERVED:
			cf_assert(vec->capacity);
			CF_FALLTHROUGH;
		case 0:
			CF_FALLTHROUGH;
		case CF_VEC_ITERATED:
			// valid flags
			break;
		case (CF_VEC_RESERVED | CF_VEC_ITERATED):
			cf_assert_fail("bad vec flags %zu\n", bits);
			break;
	}
}

/*
 * Assert that `it` is self consistent.
 *
 * check the following:
 * - `parent` is non-null
 * - 0 <= i <= parent's logical size
 */
static void
assert_vec_iter(const cf_vec_iter_t *it)
{
	cf_assert(it->parent);
	assert_vec(it->parent);
	if (it->offset == ITER_OFFSET_UNSTARTED) {
		// `it` hasn't been advanced for the first time yet
		// skip `offset` checks
		return;
	}
	cf_assert(it->offset <= it->parent->len);
	cf_assert((it->offset % it->parent->stride) == 0);
}
