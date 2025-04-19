#ifndef LOG_FILE_H
#define LOG_FILE_H

#include <stdint.h>
#include <string.h>
#include "../include/globals.h"
#include "../include/kv_store.h"

// Key-value store Command Encoding
typedef struct kvs_command_t {
    kvs_op_t type;
    uint32_t key_length;
    uint32_t value_length;

    // First stores the binary data (value), then stores the key (null terminated)
    uint8_t data[];
} kvs_command_t;

uint64_t kvs_command_len(const kvs_command_t* log_entry);
char* kvs_command_get_key(const kvs_command_t* log_entry);
void kvs_command_get_value(kvs_command_t* log_entry, uint8_t** value);

void log_file_get_data(uint64_t cmd_offset, uint8_t* out_buffer, size_t length);

#endif // LOG_FILE_H