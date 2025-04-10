#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>

#include "../include/globals.h"
#include "../include/disk_map.h"


static inline uint32_t murmur_32_scramble(uint32_t k) {
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}

static inline uint32_t murmur3_32(const uint8_t* key, size_t len) {
	uint32_t h = HASH_SEED;
    uint32_t k;
    /* Read in groups of 4. */
    for (size_t i = len >> 2; i; i--) {
        // Here is a source of differing results across endiannesses.
        // A swap here has no effects on hash properties though.
        memcpy(&k, key, sizeof(uint32_t));
        key += sizeof(uint32_t);
        h ^= murmur_32_scramble(k);
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }
    /* Read the rest. */
    k = 0;
    for (size_t i = len & 3; i; i--) {
        k <<= 8;
        k |= key[i - 1];
    }
    // A swap is *not* necessary here because the preceding loop already
    // places the low bytes in the low places according to whatever endianness
    // we use. Swaps only apply when the memory is copied in a chunk.
    h ^= murmur_32_scramble(k);
    /* Finalize. */
	h ^= len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

static inline int64_t find_free_value_slot(uint64_t* value_bitmap) {
    for (uint64_t i = 0; i < 13; i++) {
        uint64_t res = (*value_bitmap >> i) & 0b1;
        if (res == 0) {
            *value_bitmap = (*value_bitmap) | (0b1 << i);
            return i;
        }
    }
    -1;
}

uint32_t hash(const char* key) {
    return murmur3_32((uint8_t*) key, strlen(key));
}

DiskMap DiskMap_init(const char* filepath) {
    DiskMap map;

    uint64_t num_slots = 1000;
    uint64_t num_pages = (num_slots / 100) + 2;
    
    map.latches = (pthread_mutex_t*) malloc(num_pages * sizeof(pthread_mutex_t));

    for(int i = 0; i < num_pages; i++) {
        pthread_mutex_init(&map.latches[i], NULL);
    }

    return map;
}

static void __DiskMap_find_entry(
    const DiskMap* map, 
    const char* key, 
    uint64_t* page_idx_output, 
    uint8_t** page_ptr_output, 
    DiskMapEntry** slot_ptr_output
) {
    uint32_t h = hash(key);
    size_t key_length = strlen(key);

    uint64_t slot_idx = h % map->num_slots;

    uint64_t page_idx = slot_idx / 100;
    uint64_t slot_offset = slot_idx - (page_idx * 100);

    pthread_mutex_lock(&map->latches[page_idx + 1]);

    uint8_t* page_ptr = (page_idx + 1) * DM_PAGE_SIZE;

    for (int i = 0; i < KEY_NUM_SLOTS; i++) {
        uint64_t curr_slot_offset = (slot_offset+i) % 100;
        DiskMapEntry* slot_ptr = (DiskMapEntry*) (page_ptr + (curr_slot_offset * sizeof(DiskMapEntry)));

        if ((size_t) slot_ptr->key_length != key_length) {
            continue;
        }

        int res = strncmp(slot_ptr->string, key, 18);
        if (res != 0) {
            continue;
        }

        if (slot_ptr->key_length > 17) {
            uint8_t* command = NULL;
            char* log_key = (char*) (command + 1);

            res = strncmp(log_key, key, key_length);
            if (res != 0) {
                continue;
            }
        }

        // This is the correct slot!
        *page_ptr_output = page_ptr;
        *slot_ptr_output = slot_ptr;
        *page_idx_output = page_idx;
        return;
    }

    // If we've passed though the loop and have found nothing, then the key doesn't exist
    *page_ptr_output = NULL;
    *slot_ptr_output = NULL;
}

/// @brief Finds the slot 
/// @param map 
/// @param key 
/// @param page_ptr_output 
/// @param slot_ptr_output 
static void __DiskMap_find_empty_slot(
    const DiskMap* map, 
    const char* key, 
    uint64_t* page_idx_output, 
    uint8_t** page_ptr_output, 
    DiskMapEntry** slot_ptr_output
) {
    uint32_t h = hash(key);
    size_t key_length = strlen(key);

    uint64_t slot_idx = h % map->num_slots;

    uint64_t page_idx = slot_idx / 100;
    uint64_t slot_offset = slot_idx - (page_idx * 100);

    pthread_mutex_lock(&map->latches[page_idx + 1]);

    DiskMapEntry* first_empty_sp = NULL;

    uint8_t* page_ptr = (page_idx + 1) * DM_PAGE_SIZE;

    for (int i = 0; i < KEY_NUM_SLOTS; i++) {
        uint64_t curr_slot_offset = (slot_offset+i) % 100;
        DiskMapEntry* slot_ptr = (DiskMapEntry*) (page_ptr + (curr_slot_offset * sizeof(DiskMapEntry)));

        if (slot_ptr->key_length == 0) {
            if (first_empty_sp == NULL) first_empty_sp = slot_ptr;
            continue;
        }

        if ((size_t) slot_ptr->key_length != key_length) {
            continue;
        }

        int res = strncmp(slot_ptr->string, key, 18);
        if (res != 0) {
            continue;
        }

        if (slot_ptr->key_length > 17) {
            uint8_t* command = NULL;
            char* log_key = (char*) (command + 1);

            res = strncmp(log_key, key, key_length);
            if (res != 0) {
                continue;
            }
        }

        // This is the correct slot!
        *page_ptr_output = page_ptr;
        *slot_ptr_output = slot_ptr;
        *page_idx_output = page_idx;
        return;
    }

    if (first_empty_sp == NULL) {
        // If we've passed though the loop and `first_empty_sp`is still null, then there are no open slots
        *page_ptr_output = NULL;
        *slot_ptr_output = NULL;
    }
    else {
        *page_ptr_output = page_ptr;
        *slot_ptr_output = first_empty_sp;
        *page_idx_output = page_idx;
    }
}


/// @brief 
/// @param map 
/// @param key 
/// @param val_output_buffer 
/// @return The length of th evalue when the key exists and -1 if it doesn't exist
int64_t DiskMap_get(const DiskMap* map, const char* key, uint8_t val_output_buffer[VALUE_MAX_SIZE]) {
    uint64_t page_idx = 0;
    uint8_t* page_ptr = NULL;
    DiskMapEntry* slot_ptr = NULL;

    __DiskMap_find_entry(map, key, &page_idx, &page_ptr, &slot_ptr);
    if (page_ptr == NULL) {
        pthread_mutex_unlock(&map->latches[page_idx + 1]);
        return -1;
    }

    if (slot_ptr->inline_value_slot != UINT8_MAX) {
        uint8_t* value_ptr = page_ptr + 3200 + 64 + (slot_ptr->inline_value_slot * 64);
        memcpy(val_output_buffer, value_ptr, (size_t) slot_ptr->inline_value_len);

        pthread_mutex_unlock(&map->latches[page_idx + 1]);
        return (int64_t) slot_ptr->inline_value_len;
    }
    else {
        uint8_t* command = NULL;
        uint8_t* log_value = (command + slot_ptr->key_length + 2);
        uint64_t value_length = *((uint64_t*) log_value);
        log_value += 8;

        assert(value_length <= VALUE_MAX_SIZE);

        memcpy(val_output_buffer, log_value, value_length);

        pthread_mutex_unlock(&map->latches[page_idx + 1]);
        return (int64_t) value_length;
    }
}

/// @brief 
/// @param map 
/// @param key 
/// @param value 
/// @param value_len 
/// @return 
int64_t DiskMap_set(DiskMap* map, const char* key, uint8_t* value, size_t value_len) {
    uint64_t page_idx = 0;
    uint8_t* page_ptr = NULL;
    DiskMapEntry* slot_ptr = NULL;
    
    __DiskMap_find_empty_slot(map, key, &page_idx, &page_ptr, &slot_ptr);
    if (page_ptr == NULL) {
        return -1;
    }

    uint64_t command_byte_offset = 0; // TODO (get this value somehow)
    strncpy(slot_ptr->string, key, 17);
    slot_ptr->string[18] = '\0';
    slot_ptr->key_length = strlen(key);
    slot_ptr->inline_value_slot = UINT8_MAX;

    if (value_len <= 64) {
        uint64_t* inline_val_bitmap = (uint64_t*) (page_ptr + 3200);
        int64_t inline_value_slot = find_free_value_slot(inline_val_bitmap);
        if (inline_value_slot >= 0) {
            slot_ptr->inline_value_slot = (uint8_t) inline_value_slot;
            slot_ptr->inline_value_len = (uint8_t) value_len;

            uint8_t* value_slot_ptr = page_ptr + 3200 + 1 + inline_value_slot;
            memcpy(value_slot_ptr, value, value_len);
        }
    }

    pthread_mutex_unlock(&map->latches[page_idx + 1]);
    return 0;
}

/// @brief 
/// @param map 
/// @param key 
/// @return 
int64_t DiskMap_delete(DiskMap* map, const char* key) {
    uint64_t page_idx = 0;
    uint8_t* page_ptr = NULL;
    DiskMapEntry* slot_ptr = NULL;
    
    __DiskMap_find_entry(map, key, &page_idx, &page_ptr, &slot_ptr);
    if (page_ptr == NULL) {
        return -1;
    }

    slot_ptr->key_length = 0;
    if (slot_ptr->inline_value_slot != UINT8_MAX) {
        uint64_t* inline_val_bitmap = (uint64_t*) (page_ptr + 3200);
        *inline_val_bitmap = (*inline_val_bitmap) & (~(0b1 << slot_ptr->inline_value_slot));
    }

    pthread_mutex_unlock(&map->latches[page_idx + 1]);
    return 0;
}
