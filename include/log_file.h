#ifndef LOG_FILE_H
#define LOG_FILE_H

#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/globals.h"
#include "../include/kv_store.h"

// Magic number we replace the headers of the hash field with before hashing
#define MAGIC_NUMBER (XXH128_hash_t){.high64 = 15269755912704193040ULL, .low64 = 9723239452457707023ULL}

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
    kvs_command_t data[];
} kvs_batch_cmd_t;

uint64_t kvs_command_len(const kvs_command_t* log_entry);
char* kvs_command_get_key(const kvs_command_t* log_entry);
void kvs_command_get_value(kvs_command_t* log_entry, uint8_t** value);

void kvsb_header_calc_checksum(kvsb_header_t* header);
bool kvsb_header_validate_checksum(kvsb_header_t* header);

void logfile_append_data(logfile_t* logfile, cmd_buffer_t* cmd_buffer);
void logfile_get_data(logfile_t* logfile, uint64_t cmd_offset, uint8_t* out_buffer, size_t length);
void logfile_get_last_tai(logfile_t* logfile, uint64_t* term, uint64_t* index);
size_t logfile_sizeof(logfile_t* logfile);

#endif // LOG_FILE_H