/*

Copyright 2024-2025 Joachim Stolberg

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#ifndef NB_KVSTORE_H
#define NB_KVSTORE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @file nc_kvstore.h
 * @brief Key-Value Store for NanoC
 * 
 * Deterministic key-value store using sorted array with binary search.
 * - Read: O(log n) - max 7 comparisons for 100 entries
 * - Write: O(n)
 * 
 * Usage in BASIC:
 *   store = kvs_create(num_elem, def_value)
 *   kvs_set(store, key, value)
 *   value = kvs_get(store, key)
 */

/* Forward declaration of opaque store type */
typedef struct kvs_store kvs_store_t;

/**
 * @brief Calculate required memory size for a KV store
 * @param num_elem Maximum number of elements
 * @return Size in bytes needed for kvs_create()
 */
uint32_t kvs_size(uint16_t num_elem);

/**
 * @brief Create a new KV store
 * @param p_mem Pointer to pre-allocated memory (must be at least kvs_size(num_elem) bytes)
 * @param num_elem Maximum number of elements
 * @param default_val Default value returned for missing keys
 * @return Pointer to the initialized store, or NULL on error
 */
kvs_store_t *kvs_create(void *p_mem, uint16_t num_elem, int32_t default_val);

/**
 * @brief Set a key-value pair in the store
 * @param store Pointer to the KV store
 * @param key Key to set
 * @param value Value to associate with the key
 * @return 0 on success, -1 if store is full (and key doesn't exist)
 */
int kvs_set(kvs_store_t *store, int32_t key, int32_t value);

/**
 * @brief Get a value from the store
 * @param store Pointer to the KV store
 * @param key Key to look up
 * @return Value associated with key, or default_val if key not found
 */
int32_t kvs_get(kvs_store_t *store, int32_t key);

/**
 * @brief Check if a key exists in the store
 * @param store Pointer to the KV store
 * @param key Key to check
 * @return true if key exists, false otherwise
 */
bool kvs_exists(kvs_store_t *store, int32_t key);

/**
 * @brief Get the current number of entries in the store
 * @param store Pointer to the KV store
 * @return Number of entries
 */
uint16_t kvs_count(kvs_store_t *store);

/**
 * @brief Clear all entries from the store
 * @param store Pointer to the KV store
 */
void kvs_clear(kvs_store_t *store);

/**
 * @brief Delete a key from the store
 * @param store Pointer to the KV store
 * @param key Key to delete
 * @return 0 if key was deleted, -1 if key was not found
 */
int kvs_delete(kvs_store_t *store, int32_t key);

#endif /* NB_KVSTORE_H */
