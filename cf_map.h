/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * Basic flat map library.
 */
#pragma once

#include "cf_vector.h"

#include "cc_support.h"

#include <stdbool.h>
#include <stdint.h>

__BEGIN_DECLS

/*
 * An entry in a `cf_map8_t`.
 *
 * A 64bit key and value glued together.
 */
typedef struct {
	uint64_t key;
	uint64_t value;
} cf_map_entry_t;

/*
 * A simple map of opaque 64bit int keys and values.
 *
 * This is lazily implemented as a vector. Search is a linear time operation,
 * but this is fine for cfind because its maps aren't usually very big.
 *
 * Also note that insertion is constant time. The consequence of this is that
 * keys aren't checked for uniqueness. Two insertions that use the same key
 * cause the first entry to shadow the second one. The second entry won't ever
 * be returned by a lookup until _remove() is called on the first one. This
 * doesn't matter for cfind because it usually uses pointers or monotonically
 * increasing integers for keys.
 *
 * Reproduced for convenience, the following functions are generated:
 *   cf_map8_make
 *   cf_map8_free
 *   cf_map8_reserve
 *   cf_map8_commit
 *   cf_map8_len
 *
 * Use these along with the _lookup() and _remove() functions declared below.
 */
CF_VEC_GENERATE(cf_map8_t, cf_map_entry_t, cf_map8);

bool cf_map8_lookup(cf_map8_t *map, uint64_t key, uint64_t *out);
bool cf_map8_remove(cf_map8_t *map, uint64_t key);

__END_DECLS
