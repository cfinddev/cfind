/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 */
#include "cf_map.h"

#include "cf_vector.h"

#include <stdbool.h>
#include <stdint.h>

CF_VEC_ITER_GENERATE(cf_map8_t, cf_map_entry_t, map8_iter);

static bool lookup_internal(cf_map8_t *map, uint64_t key,
		cf_map_entry_t **out);

/*
 * Search through `map` for an entry equal to `key` then return its value.
 *
 * On success, return `true` and set `*out` to the value.
 */
bool
cf_map8_lookup(cf_map8_t *map, uint64_t key, uint64_t *out)
{
	cf_map_entry_t *entry;
	if (!lookup_internal(map, key, &entry)) {
		return false;
	}
	*out = entry->value;
	return true;
}

/*
 * Search `map` for an entry equal to `key` then remove it.
 *
 * Return `true` if an entry matching `key` was found.
 */
bool
cf_map8_remove(cf_map8_t *map, uint64_t key)
{
	cf_map_entry_t *entry;
	if (!lookup_internal(map, key, &entry)) {
		return false;
	}

	cf_vec_remove(&map->v, entry);

	return true;
}

/*
 * Do a lookup in `map` for `key`; return the entry via `*out`.
 *
 * NOTE: slightly unsafe because CF_VEC_ITERATED is cleared when this function
 * returns and yet a pointer to an entry is returned. However, this is fine for
 * a `static` function in a single threaded program. (It's easy to see in the
 * source there's no map insertion interleaved between lookup and use.)
 */
static bool
lookup_internal(cf_map8_t *map, uint64_t key, cf_map_entry_t **out)
{
	if (!cf_map8_len(map)) {
		return false;
	}

	cf_vec_iter_t iter;
	map8_iter_make(map, &iter);

	// iterate over every entry
	bool found = false;
	while (map8_iter_next(&iter)) {
		cf_map_entry_t *entry = map8_iter_peek(&iter);
		if (entry->key == key) {
			// a match
			*out = entry;
			found = true;
			break;
		}
	}

	map8_iter_free(&iter);
	return found;
}
