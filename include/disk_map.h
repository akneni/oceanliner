#ifndef DISK_MAP_H
#define DISK_MAP_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#define HASH_SEED 42
#define DM_PAGE_SIZE 4096

typedef struct DiskMap {

    // Copies of metadata to ensure they stay in memory
    uint64_t last_committed_index;
    uint64_t last_committed_term;
    uint64_t num_slots;
    uint64_t num_entries;

    // Mempry mapped to disk. 
    uint8_t* data;

    // latches to ensure thread saftey
    pthread_mutex_t* latches;
} DiskMap;

// 32 bytes
typedef struct DiskMapEntry {
    uint64_t cmd_byte_offset;
    uint32_t key_length;        // Is this field is 0, then this slot is empty
    char string[18];
    uint8_t inline_value_slot;  // UINT8_MAX if value is not inlined
    uint8_t inline_value_len;
} DiskMapEntry;

uint32_t hash(const char* key);

#endif // DISK_MAP_H