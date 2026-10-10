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

/**
 * @file nc_kvstore.c
 * @brief Key-Value Store for NanoC
 * 
 * Implements a deterministic key-value store using a sorted array with binary search.
 * - Read access: O(log n) - max 7 comparisons for 100 entries
 * - Write access: O(n) - negligible for small entry counts
 * - Memory: minimal overhead, just header + n * 2 * sizeof(int32_t)
 * 
 * Usage in BASIC:
 *   store = kvs_create(num_elem, def_value)
 *   kvs_set(store, key, value)
 *   value = kvs_get(store, key)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * KV-Store structure layout in memory:
 * 
 * ┌──────────────────────────────────────────────────┐
 * │  count (uint16_t)     - Current number of entries│
 * │  capacity (uint16_t)  - Maximum entries          │
 * │  default_val (int32_t)- Default return value     │
 * ├──────────────────────────────────────────────────┤
 * │  key[0] (int32_t)  │  value[0] (int32_t)         │
 * │  key[1] (int32_t)  │  value[1] (int32_t)         │
 * │  ...               │  ...                        │
 * │  key[n-1]          │  value[n-1]                 │
 * └──────────────────────────────────────────────────┘
 * 
 * Keys are stored in ascending sorted order for binary search.
 */

typedef struct {
    int32_t key;
    int32_t value;
} kvs_entry_t;

typedef struct kvs_store {
    uint16_t count;       // Current number of entries
    uint16_t capacity;    // Maximum number of entries
    int32_t  default_val; // Default value for missing keys
    kvs_entry_t entries[]; // Flexible array member
} kvs_store_t;

/**
 * @brief Calculate required memory size for a KV store
 * @param num_elem Maximum number of elements
 * @return Size in bytes
 */
uint32_t kvs_size(uint16_t num_elem) {
    return sizeof(kvs_store_t) + (uint32_t)num_elem * sizeof(kvs_entry_t);
}

/**
 * @brief Binary search for a key in the store
 * @param store Pointer to the KV store
 * @param key Key to search for
 * @param p_idx Output: index where key is or should be inserted
 * @return true if key was found, false otherwise
 */
static bool kvs_bsearch(kvs_store_t *store, int32_t key, uint16_t *p_idx) {
    if (store->count == 0) {
        *p_idx = 0;
        return false;
    }
    
    uint16_t left = 0;
    uint16_t right = store->count;
    
    while (left < right) {
        uint16_t mid = left + (right - left) / 2;
        int32_t mid_key = store->entries[mid].key;
        
        if (mid_key == key) {
            *p_idx = mid;
            return true;
        } else if (mid_key < key) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    *p_idx = left;
    return false;
}

/**
 * @brief Create a new KV store
 * @param p_mem Pointer to pre-allocated memory (must be at least kvs_calc_size(num_elem) bytes)
 * @param num_elem Maximum number of elements
 * @param default_val Default value returned for missing keys
 * @return Pointer to the initialized store, or NULL on error
 */
kvs_store_t *kvs_create(void *p_mem, uint16_t num_elem, int32_t default_val) {
    if (p_mem == NULL || num_elem == 0) {
        return NULL;
    }
    
    kvs_store_t *store = (kvs_store_t *)p_mem;
    store->count = 0;
    store->capacity = num_elem;
    store->default_val = default_val;
    
    return store;
}

/**
 * @brief Set a key-value pair in the store
 * @param store Pointer to the KV store
 * @param key Key to set
 * @param value Value to associate with the key
 * @return 0 on success, -1 if store is full (and key doesn't exist)
 */
int kvs_set(kvs_store_t *store, int32_t key, int32_t value) {
    if (store == NULL) {
        return -1;
    }
    
    uint16_t idx;
    bool found = kvs_bsearch(store, key, &idx);
    
    if (found) {
        // Key exists, update value
        store->entries[idx].value = value;
        return 0;
    }
    
    // Key doesn't exist, need to insert
    if (store->count >= store->capacity) {
        return -1; // Store is full
    }
    
    // Shift entries to make room for new key (insert at idx)
    // Move entries[idx..count-1] to entries[idx+1..count]
    if (idx < store->count) {
        memmove(&store->entries[idx + 1], 
                &store->entries[idx], 
                (store->count - idx) * sizeof(kvs_entry_t));
    }
    
    // Insert new entry
    store->entries[idx].key = key;
    store->entries[idx].value = value;
    store->count++;
    
    return 0;
}

/**
 * @brief Get a value from the store
 * @param store Pointer to the KV store
 * @param key Key to look up
 * @return Value associated with key, or default_val if key not found
 */
int32_t kvs_get(kvs_store_t *store, int32_t key) {
    if (store == NULL) {
        return 0;
    }
    
    uint16_t idx;
    if (kvs_bsearch(store, key, &idx)) {
        return store->entries[idx].value;
    }
    
    return store->default_val;
}

/**
 * @brief Check if a key exists in the store
 * @param store Pointer to the KV store
 * @param key Key to check
 * @return true if key exists, false otherwise
 */
bool kvs_exists(kvs_store_t *store, int32_t key) {
    if (store == NULL) {
        return false;
    }
    
    uint16_t idx;
    return kvs_bsearch(store, key, &idx);
}

/**
 * @brief Get the current number of entries in the store
 * @param store Pointer to the KV store
 * @return Number of entries
 */
uint16_t kvs_count(kvs_store_t *store) {
    if (store == NULL) {
        return 0;
    }
    return store->count;
}

/**
 * @brief Clear all entries from the store
 * @param store Pointer to the KV store
 */
void kvs_clear(kvs_store_t *store) {
    if (store != NULL) {
        store->count = 0;
    }
}

/**
 * @brief Delete a key from the store
 * @param store Pointer to the KV store
 * @param key Key to delete
 * @return 0 if key was deleted, -1 if key was not found
 */
int kvs_delete(kvs_store_t *store, int32_t key) {
    if (store == NULL) {
        return -1;
    }
    
    uint16_t idx;
    if (!kvs_bsearch(store, key, &idx)) {
        return -1; // Key not found
    }
    
    // Shift entries to fill the gap
    // Move entries[idx+1..count-1] to entries[idx..count-2]
    if (idx < store->count - 1) {
        memmove(&store->entries[idx], 
                &store->entries[idx + 1], 
                (store->count - idx - 1) * sizeof(kvs_entry_t));
    }
    
    store->count--;
    return 0;
}

#ifdef KVS_TEST
/*
 * Simple test program - compile with:
 * gcc -DKVS_TEST -o kvs_test nc_kvstore.c
 */
#include <stdio.h>
#include <assert.h>

int main(void) {
    // Allocate memory for 100 entries
    uint8_t mem[sizeof(kvs_store_t) + 100 * sizeof(kvs_entry_t)];
    
    kvs_store_t *store = kvs_create(mem, 100, -1);
    assert(store != NULL);
    assert(kvs_count(store) == 0);
    
    // Test basic set/get
    assert(kvs_set(store, 50, 500) == 0);
    assert(kvs_get(store, 50) == 500);
    assert(kvs_count(store) == 1);
    
    // Test default value for missing key
    assert(kvs_get(store, 99) == -1);
    
    // Test update existing key
    assert(kvs_set(store, 50, 501) == 0);
    assert(kvs_get(store, 50) == 501);
    assert(kvs_count(store) == 1);
    
    // Test insertion order (sorted)
    assert(kvs_set(store, 10, 100) == 0);
    assert(kvs_set(store, 90, 900) == 0);
    assert(kvs_set(store, 30, 300) == 0);
    assert(kvs_set(store, 70, 700) == 0);
    assert(kvs_count(store) == 5);
    
    // Verify all values
    assert(kvs_get(store, 10) == 100);
    assert(kvs_get(store, 30) == 300);
    assert(kvs_get(store, 50) == 501);
    assert(kvs_get(store, 70) == 700);
    assert(kvs_get(store, 90) == 900);
    
    // Test exists
    assert(kvs_exists(store, 50) == true);
    assert(kvs_exists(store, 51) == false);
    
    // Test delete
    assert(kvs_delete(store, 50) == 0);
    assert(kvs_exists(store, 50) == false);
    assert(kvs_get(store, 50) == -1);
    assert(kvs_count(store) == 4);
    
    // Test delete non-existent key
    assert(kvs_delete(store, 999) == -1);
    
    // Test clear
    kvs_clear(store);
    assert(kvs_count(store) == 0);
    assert(kvs_get(store, 10) == -1);
    
    printf("All tests passed!\n");
    return 0;
}
#endif
