#ifndef LOG_FILE_H
#define LOG_FILE_H

#include <stdint.h>
#include <string.h>
#include "../include/globals.h"
#include "../include/kv_store.h"

// Magic number we replace the headers of the hash field with before hashing
#define MAGIC_NUMBER (XXH128_hash_t){.high64 = 15269755912704193040ULL, .low64 = 9723239452457707023ULL}

// Key-value store Command Encoding
typedef struct kvs_command_t {
    uint32_t key_length;
    uint32_t value_length;
    kvs_op_t type;
    uint8_t padding[3];

    // First stores the binary data (value), then stores the key (null terminated)
    uint8_t data[];
} kvs_command_t;

// Header for the key-value store batch
typedef struct {
    uint64_t term;
    uint64_t num_commands;
    uint64_t log_index;
    size_t data_length;         // Length of all the commands in bytes
    XXH128_hash_t header_checsum;
} kvsb_header_t;

// batch od key value store commands 
typedef struct {
    kvsb_header_t header;
    uint8_t data[];
} kvs_batch_cmd_t;

uint64_t kvs_command_len(const kvs_command_t* log_entry);
char* kvs_command_get_key(const kvs_command_t* log_entry);
void kvs_command_get_value(kvs_command_t* log_entry, uint8_t** value);

void log_file_get_data(uint64_t cmd_offset, uint8_t* out_buffer, size_t length);

void kvsb_header_calc_checksum(kvsb_header_t* header);
bool kvsb_header_validate_checksum(kvsb_header_t* header);

#endif // LOG_FILE_H