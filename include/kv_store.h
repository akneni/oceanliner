#ifndef KV_STORE_H
#define KV_STORE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>

#include "../include/globals.h"
#include "../include/xxhash.h"

#define HASH_SEED 42

// Number of slots to check in a SET op before triggering a rehash
#define KEY_NUM_SLOTS 32

// Number of kvs_entry slots per page (4096 bytes)
#define KVE_NUM_SLOTS 128
// Number of kvs_entry slots per page (4096 bytes) (represented as log 2)
#define KVE_NUM_SLOTS_LOG2 7


#define INLINE_KV_OFFSET 2050

// Size in bytes of the inline value slot
#define IKVS_SIZE 64

// The number of inline value slots
#define IVS_NUM_SLOTS 15

// Key-value store operation
typedef enum kvs_op_t {
    CMD_NOP,
    CMD_GET,
    CMD_SET,
    CMD_DELETE,
} kvs_op_t;

typedef struct inline_val_slot {
    uint8_t data[64];
} inline_val_slot;

typedef struct kvs_page_t {
    // Command byte offset and inline value slot
    uint64_t cbo_and_ivs[KVE_NUM_SLOTS];
    XXH128_hash_t key_hash[KVE_NUM_SLOTS];
    inline_val_slot inline_vals[IVS_NUM_SLOTS];
    pthread_mutex_t latch;
    uint8_t inline_vals_len[IVS_NUM_SLOTS];
    uint8_t padding[9];
} kvs_page_t;


typedef struct kv_store_t {
    // Copies of metadata to ensure they stay in memory
    uint64_t last_committed_index;
    uint64_t last_committed_term;

    // Represented as a power of 2
    uint64_t num_slots_log2;

    // mmap()ed hashmap data.
    kvs_page_t* data;
} kv_store_t;

kv_store_t kv_store_init(const char* filepath, uint8_t* log_file);
uint8_t* kv_store_get(const kv_store_t* map, const char* key, int64_t* value_length);
int64_t kv_store_set(kv_store_t* map, uint64_t cmd_byte_offset, const char* key, uint8_t* value, size_t value_len);
int64_t kv_store_delete(kv_store_t* map, const char* key);
void kv_store_display_values(const kv_store_t* map);

#endif // KV_STORE_H
