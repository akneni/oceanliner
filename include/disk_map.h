#ifndef DISK_MAP_H
#define DISK_MAP_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include "../include/globals.h"

#define HASH_SEED 42

// Number of slots to check in a SET op before triggering a rehash
#define KEY_NUM_SLOTS 32

#define DM_PAGE_SIZE 4096
#define INLINE_KV_OFFSET 2050

// Size in bytes of the inline key-value slot
#define IKVS_SIZE 127

// The number of inline key-value slots
#define IKVS_NUM_SLOTS 16

typedef struct DiskMap {

    // Copies of metadata to ensure they stay in memory
    uint64_t last_committed_index;
    uint64_t last_committed_term;
    
    // Represented as a power of 2
    uint64_t num_slots;

    // Represented in regular base 10
    uint64_t num_entries;

    // mmap()ed hashmap data. 
    uint8_t* data;

    // mmap()ed log file. 
    uint8_t* log_file;

    // latches to ensure thread saftey
    pthread_mutex_t* latches;
} DiskMap;

typedef struct DiskMapEntry {
    uint64_t cmd_byte_offset;
    uint32_t key_hash;
    uint16_t key_length;        // Is this field is 0, then this slot is empty
    uint8_t inline_kv_slot;     // UINT8_MAX if value is not inlined
    uint8_t inline_value_len;
} DiskMapEntry;



uint32_t hash(const char* key);
DiskMap DiskMap_init(const char* filepath, uint8_t* log_file);
int64_t DiskMap_get(const DiskMap* map, const char* key, uint8_t val_output_buffer[VALUE_MAX_SIZE]);
int64_t DiskMap_set(DiskMap* map, uint64_t cmd_byte_offset, const char* key, uint8_t* value, size_t value_len);
int64_t DiskMap_delete(DiskMap* map, const char* key);
void DiskMap_display_values(const DiskMap* map);

#endif // DISK_MAP_H