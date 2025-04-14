#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>

#include "../include/globals.h"
#include "../include/disk_map.h"
#include "../include/log_file.h"


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
    return -1;
}

uint32_t hash(const char* key) {
    return murmur3_32((uint8_t*) key, strlen(key));
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
    *page_idx_output = page_idx;

    uint64_t slot_offset = slot_idx - (page_idx * 100);

    pthread_mutex_lock(&map->latches[page_idx + 1]);

    uint8_t* page_ptr = map->data + ((page_idx + 1) * DM_PAGE_SIZE);

    for (int i = 0; i < KEY_NUM_SLOTS; i++) {
        uint64_t curr_slot_offset = (slot_offset+i) % 100;
        DiskMapEntry* slot_ptr = (DiskMapEntry*) (page_ptr + (curr_slot_offset * sizeof(DiskMapEntry)));

        if (h != slot_ptr->key_hash) {
            continue;
        }

        if ((size_t) slot_ptr->key_length != key_length) {
            continue;
        }

        // Final key comparision 
        if (slot_ptr->inline_kv_slot != UINT8_MAX) {
            char* log_key = page_ptr + (INLINE_VALS_OFFSET + slot_ptr->inline_kv_slot * 127 + slot_ptr->inline_value_len);
            char* res = strncmp(log_key, key, key_length);
            if (res != 0) {
                continue;
            }
        }
        else {
            uint8_t command[4096];
            LogFile_get_data(slot_ptr->cmd_byte_offset, command, 4096);
            
            char* log_key = (char*) (command + 1);

            char* res = strncmp(log_key, key, key_length);
            if (res != 0) {
                continue;
            }
        }

        // This is the correct slot!
        *page_ptr_output = page_ptr;
        *slot_ptr_output = slot_ptr;
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
    *page_idx_output = page_idx;

    uint64_t slot_offset = slot_idx - (page_idx * 100);

    pthread_mutex_lock(&map->latches[page_idx + 1]);

    DiskMapEntry* first_empty_sp = NULL;

    uint8_t* page_ptr = map->data + ((page_idx + 1) * DM_PAGE_SIZE);

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

        int res = strncmp(slot_ptr->string, key, 17);
        if (res != 0) {
            continue;
        }

        if (slot_ptr->key_length > 17) {
            uint8_t command[4096];
            LogFile_get_data(slot_ptr->cmd_byte_offset, command, 4096);
            
            char* log_key = (char*) (command + 1);
            res = strncmp(log_key, key, key_length);
            if (res != 0) {
                continue;
            }
        }

        // Found an existing key that we'll replace
        *page_ptr_output = page_ptr;
        *slot_ptr_output = slot_ptr;
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
    }
}

DiskMap DiskMap_init(const char* filepath, uint8_t* log_file) {
    DiskMap map;

    map.num_slots = 1000;
    uint64_t num_pages = (map.num_slots / 100) + 2;
    
    map.log_file = log_file;
    map.latches = (pthread_mutex_t*) malloc(num_pages * sizeof(pthread_mutex_t));

    size_t data_size = num_pages * DM_PAGE_SIZE;
    map.data = malloc(data_size);
    memset(map.data, 0, data_size);

    for(int i = 0; i < num_pages; i++) {
        pthread_mutex_init(&map.latches[i], NULL);
    }

    return map;
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

    if (slot_ptr->inline_kv_slot != UINT8_MAX) {
        uint8_t* value_ptr = page_ptr + INLINE_KV_OFFSET + (slot_ptr->inline_kv_slot * 64);
        memcpy(val_output_buffer, value_ptr, (size_t) slot_ptr->inline_value_len);

        pthread_mutex_unlock(&map->latches[page_idx + 1]);
        return (int64_t) slot_ptr->inline_value_len;
    }
    else {
        // uint8_t* command = &map->log_file[slot_ptr->cmd_byte_offset];
        uint8_t command[4096];
        LogFile_get_data(slot_ptr->cmd_byte_offset, command, 4096);
        
        uint8_t* log_value = (command + slot_ptr->key_length + 2);
        uint64_t byte_diff = ((uint64_t) log_value) % 8;
        log_value = (uint8_t*) (((uint64_t) log_value) + (8-byte_diff));

        uint64_t value_length = *((uint64_t*) log_value);
        log_value += 8;

        assert(value_length <= VALUE_MAX_SIZE);

        memcpy(val_output_buffer, log_value, value_length);

        pthread_mutex_unlock(&map->latches[page_idx + 1]);
        return (int64_t) value_length;
    }

    assert(false);
}

/// @brief 
/// @param map 
/// @param key 
/// @param value 
/// @param value_len 
/// @return 
int64_t DiskMap_set(DiskMap* map, uint64_t cmd_byte_offset, const char* key, uint8_t* value, size_t value_len) {
    uint64_t page_idx = 0;
    uint8_t* page_ptr = NULL;
    DiskMapEntry* slot_ptr = NULL;

    size_t key_length = strlen(key);

    assert(key_length < UINT32_MAX);
    assert(key_length > 0);
    assert(value_len < VALUE_MAX_SIZE);
    
    __DiskMap_find_empty_slot(map, key, &page_idx, &page_ptr, &slot_ptr);
    if (page_ptr == NULL) {
        perror("failed to find slot.\n");
        exit(1);
        pthread_mutex_unlock(&map->latches[page_idx + 1]);
        return -1;
    }
    assert(slot_ptr != NULL);

    strncpy(slot_ptr->string, key, 17);
    slot_ptr->string[17] = '\0';
    slot_ptr->key_length = key_length;
    slot_ptr->inline_value_slot = UINT8_MAX;
    slot_ptr->cmd_byte_offset = cmd_byte_offset;

    if (value_len <= 64) {
        uint64_t* inline_val_bitmap = (uint64_t*) (page_ptr + 3200);
        int64_t inline_value_slot = find_free_value_slot(inline_val_bitmap);
        if (inline_value_slot >= 0) {
            slot_ptr->inline_value_slot = (uint8_t) inline_value_slot;
            slot_ptr->inline_value_len = (uint8_t) value_len;

            uint8_t* value_slot_ptr = page_ptr + INLINE_KV_OFFSET + (inline_value_slot*64);
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
        pthread_mutex_unlock(&map->latches[page_idx + 1]);
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



/// @brief This is is to be used for debugging ONLY.
/// @param map 
void DiskMap_display_values(const DiskMap* map) {
    assert(map->data != NULL);
    
    uint64_t counter = 0;
    for(uint64_t page_idx = 1; page_idx < (map->num_slots / 100) + 1; page_idx++) {
        uint8_t* page = &map->data[page_idx * DM_PAGE_SIZE];
        assert(page != NULL);
        
        for (uint64_t slot_idx = 0; slot_idx < 100; slot_idx++) {
            DiskMapEntry* entry = &((DiskMapEntry*) page)[slot_idx];
            assert(entry != NULL);

            if (entry->key_length != 0) {
                counter++;

                printf("[%lu] (%s) (", counter, entry->string);
                if (entry->inline_value_slot == UINT8_MAX) {
                    printf("no inline value)\n");
                    continue;
                }
                
                uint64_t iv_offset = INLINE_KV_OFFSET + (entry->inline_value_slot * 64);
                uint8_t* inline_value = page + iv_offset;

                for(int i = 0; i < entry->inline_value_len; i++) {
                    printf("%hu ", inline_value[i]);
                }
                printf(")\n");

            }
        }
    }
}
