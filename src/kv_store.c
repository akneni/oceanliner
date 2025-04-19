#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <immintrin.h>

#include "../include/globals.h"
#include "../include/kv_store.h"
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

static inline int64_t find_free_value_slot(uint8_t* ivs_len_arr) {
    for (uint64_t i = 0; i < IVS_NUM_SLOTS; i++) {
        if (ivs_len_arr[i] == UINT8_MAX) {
            return i;
        }
    }
    return -1;
}

// TODO: choose a real 128 bit hash function
static hash_128bi hash(const char* key) {
    uint64_t h = (uint64_t) murmur3_32((uint8_t*) key, strlen(key));

    hash_128bi h_128;
    h_128.hash_p1 = h | (h << sizeof(uint32_t));
    h_128.hash_p2 = h | (h << sizeof(uint32_t));
    return h_128;
}

/// @brief
/// @param key
/// @param page_idx
/// @param slot_idx
/// @param num_slots Expected to be passed as a power of 2. So passing `10` for this argument would mean that there are 1024 slots.
static inline void hash_to_slot(const hash_128bi* h, uint64_t* page_idx, uint64_t* slot_idx, uint64_t num_slots_log2) {
    *page_idx = h->hash_p1 >> (sizeof(*h) - num_slots_log2);
    *slot_idx = h->hash_p1 >> (sizeof(*h) - KVE_NUM_SLOTS_LOG2);

    assert(*slot_idx < 128);
}

static inline bool key_hash_cmp(const hash_128bi* h1, const hash_128bi* h2) {
    #ifdef __AVX2__
        __m128i h1_simd = _mm_load_si128((__m128i*) h1);
        __m128i h2_simd = _mm_load_si128((__m128i*) h2);

        __m128i cmp = _mm_cmpeq_epi8(h1_simd, h2_simd);
        __m128i zero = _mm_setzero_si128();

        return _mm_testc_si128(zero, cmp) == 1;
    #else
        return (h1->hash_p1 == h2->hash_p1 && h1->hash_p2 == h2->hash_p2);
    #endif
}

/// @brief Finds the slot with the specified key and aquires a lock on that page if a free slot is available.
/// @param map
/// @param key
/// @param page_idx_output
/// @param page_ptr_output
/// @param slot_ptr_output
static bool __kv_store_find_entry(
    const kv_store_t* map,
    const hash_128bi* key_hash,
    uint64_t* page_idx_output,
    uint64_t* slot_idx_output
) {
    uint64_t page_idx;
    uint64_t slot_idx;
    hash_to_slot(key_hash, &page_idx, &slot_idx, map->num_slots_log2);
    *page_idx_output = page_idx;


    kvs_page_t* page_ptr = &map->data[page_idx];
    pthread_mutex_lock(&page_ptr->latch);

    for (uint64_t i = 0; i < KEY_NUM_SLOTS; i++) {
        uint64_t curr_slot_idx = (slot_idx+i) & 0b0111111;

        if (key_hash_cmp(key_hash, &page_ptr->key_hash[curr_slot_idx]) || page_ptr->cbo_and_ivs[curr_slot_idx] != UINT64_MAX) {
            *slot_idx_output = curr_slot_idx;
            return true;
        }
    }

    pthread_mutex_unlock(&page_ptr->latch);
    return false;
}

/// @brief Finds the slot of the matching key or the first empty slot. Aquires a lock on the page if a slot is found
/// @param map
/// @param key
/// @param page_ptr_output
/// @param slot_ptr_output
static bool __kv_store_find_empty_slot(
    const kv_store_t* map,
    const hash_128bi* key_hash,
    uint64_t* page_idx_output,
    uint64_t* slot_idx_output
) {
    uint64_t page_idx;
    uint64_t slot_idx;
    hash_to_slot(key_hash, &page_idx, &slot_idx, map->num_slots_log2);

    *page_idx_output = page_idx;
    *slot_idx_output = UINT64_MAX;

    kvs_page_t* page_ptr = &map->data[page_idx];

    pthread_mutex_lock(&page_ptr->latch);

    for (int i = 0; i < KEY_NUM_SLOTS; i++) {
        uint64_t curr_slot_offset = (slot_idx+i) & 0b0111111;

        if (page_ptr->cbo_and_ivs[i] == UINT64_MAX) {
            if (*slot_idx_output == UINT64_MAX) *slot_idx_output = curr_slot_offset;
            continue;
        }

        if (key_hash_cmp(key_hash, &page_ptr->key_hash[curr_slot_offset])) {
            *slot_idx_output = curr_slot_offset;
            return true;
        }
    }

    if (*slot_idx_output == UINT64_MAX) {
        pthread_mutex_unlock(&page_ptr->latch);
        return false;
    }
    return true;
}

static inline uint64_t extract_ivs(uint64_t num) {
    return num & 0xFF; // Extract lower 8 bits
}

static inline uint64_t extract_cbo(uint64_t num) {
    return num >> 8; // Extract upper 56 bits
}

kv_store_t kv_store_init(const char* filepath, uint8_t* log_file) {
    kv_store_t map;

    map.num_slots_log2 = 10;
    uint64_t num_pages = (1 << (map.num_slots_log2 - KVE_NUM_SLOTS_LOG2)) + 1;


    size_t data_size = num_pages * sizeof(kvs_page_t);
    map.data = (kvs_page_t*) malloc(data_size);

    // Set all slots to be empty
    for(int i = 0; i < num_pages-1; i++) {
        kvs_page_t* page = &map.data[i];
        for (int j = 0; j < KVE_NUM_SLOTS; j++) {
            page->cbo_and_ivs[j] = UINT64_MAX;
        }
    }

    // Initialize all page-level latches
    for(int i = 0; i < num_pages-1; i++) {
        kvs_page_t* page = &map.data[i];
        pthread_mutex_init(&page->latch, NULL);
    }

    return map;
}

/// @brief
/// @param map
/// @param key
/// @param val_output_buffer
/// @return The length of th evalue when the key exists and -1 if it doesn't exist
uint8_t* kv_store_get(const kv_store_t* map, const char* key, int64_t* value_length) {
    hash_128bi key_hash = hash(key);
    uint64_t page_idx;
    uint64_t slot_idx;

    bool slot_found = __kv_store_find_entry(map, &key_hash, &page_idx, &slot_idx);
    if (!slot_found) {
        // Mutex is already unlocked if no slot is found
        *value_length = -1;
        return NULL;
    }

    kvs_page_t* page = &map->data[page_idx];


    uint64_t cbo = extract_cbo(page->cbo_and_ivs[slot_idx]);
    uint64_t ivs = extract_ivs(page->cbo_and_ivs[slot_idx]);

    if (ivs != UINT8_MAX) {
        inline_val_slot* value_ptr = &page->inline_vals[ivs];

        uint8_t* value_data = (uint8_t*) malloc(page->inline_vals_len[ivs]);
        memcpy(value_data, value_ptr->data, (size_t) page->inline_vals_len[ivs]);

        pthread_mutex_unlock(&page->latch);
        *value_length = (int64_t) page->inline_vals_len[ivs];
        return value_data;
    }
    else {
        uint8_t command[4096];
        kvs_command_t* kvsc = (kvs_command_t*) command;
        log_file_get_data(cbo, command, 4096);

        uint8_t* value_data = (uint8_t*) malloc(kvsc->value_length);
        memcpy(value_data, kvsc->data, kvsc->value_length);

        pthread_mutex_unlock(&page->latch);
        return value_data;
    }
}

/// @brief
/// @param map
/// @param key
/// @param value
/// @param value_len
/// @return
int64_t kv_store_set(kv_store_t* map, uint64_t cmd_byte_offset, const char* key, uint8_t* value, size_t value_len) {
    hash_128bi key_hash = hash(key);
    uint64_t page_idx;
    uint64_t slot_idx;

    bool found_slot = __kv_store_find_empty_slot(map, &key_hash, &page_idx, &slot_idx);
    if (!found_slot) {
        perror("failed to find slot.\n");
        exit(1);
        return -1;
    }

    kvs_page_t* page = &map->data[page_idx];

    page->key_hash[slot_idx] = key_hash;

    uint64_t ivs = UINT8_MAX;
    uint64_t cbo = cmd_byte_offset;

    // Inline the value if it fits
    if (value_len <= IKVS_SIZE) {
        assert(IKVS_SIZE <= UINT8_MAX);

        int64_t free_ivs = find_free_value_slot(page->inline_vals_len);

        if (free_ivs >= 0) {
            ivs = (uint64_t) free_ivs;

            memcpy(&page->inline_vals[ivs].data, value, value_len);
            page->inline_vals_len[ivs] = (uint8_t) value_len;
        }
    }

    assert(ivs <= UINT8_MAX);
    page->cbo_and_ivs[slot_idx] = (cbo << 8) | ivs;

    pthread_mutex_unlock(&page->latch);
    return 0;
}

/// @brief
/// @param map
/// @param key
/// @return
int64_t kv_store_delete(kv_store_t* map, const char* key) {
    hash_128bi key_hash = hash(key);
    uint64_t page_idx;
    uint64_t slot_idx;

    bool slot_found = __kv_store_find_entry(map, &key_hash, &page_idx, &slot_idx);
    if (!slot_found) {
        // Mutex is already unlocked if no slot is found
        return 0;
    }

    kvs_page_t* page = &map->data[page_idx];

    uint64_t ivs = extract_ivs(page->cbo_and_ivs[slot_idx]);
    if (ivs != UINT8_MAX) {
        page->inline_vals_len[ivs] = UINT8_MAX;
    }

    page->cbo_and_ivs[slot_idx] = UINT64_MAX;
    pthread_mutex_unlock(&page->latch);
    return 0;
}

/// @brief This is is to be used for debugging ONLY.
/// @param map
void kv_store_display_values(const kv_store_t* map) {
    assert(map->data != NULL);

    uint8_t buffer[4096];
    kvs_command_t* cmd_buffer = (kvs_command_t*) buffer;

    uint64_t counter = 0;
    for(uint64_t page_idx = 1; page_idx < (map->num_slots_log2 / 100) + 1; page_idx++) {
        kvs_page_t* page = &map->data[page_idx];

        for (uint64_t slot_idx = 0; slot_idx < KVE_NUM_SLOTS; slot_idx++) {
            if (page->cbo_and_ivs[slot_idx] != UINT64_MAX) {
                counter++;

                uint64_t cbo = extract_cbo(page->cbo_and_ivs[slot_idx]);

                log_file_get_data(cbo, buffer, 4096);

                printf("[%lu] (%s) (", counter, (char*) (&cmd_buffer->data[cmd_buffer->value_length]));

                uint8_t* value = cmd_buffer->data;
                size_t val_len = (cmd_buffer->value_length > 100) ? 100 : cmd_buffer->value_length;

                for(int i = 0; i < val_len; i++) {
                    printf("%hu ", value[i]);
                }
                printf(")\n");

            }
        }
    }
}
