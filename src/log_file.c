#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include "../include/log_file.h"
#include "../include/globals.h"
#include "../include/xxhash.h"

uint64_t kvs_command_len(const kvs_command_t* log_entry) {
    assert(((uint64_t) log_entry) % 8 == 0);

    return (
        4 + // kvs_op_t type + padding
        4 + // uint32_t key_length
        4 + // uint32_t value_size
        (uint64_t) log_entry->key_length +
        1 + // null terminator 
        (uint64_t) log_entry->value_length
    );
}

char* kvs_command_get_key(const kvs_command_t* log_entry) {
    assert(((uint64_t) log_entry) % 8 == 0);
    return (char*) log_entry->data;
}

void kvs_command_get_value(kvs_command_t* log_entry, uint8_t** value) {
    assert(((uint64_t) log_entry) % 8 == 0);
    *value = (log_entry->data + log_entry->key_length + 1);
}

void log_file_get_data(uint64_t cmd_offset, uint8_t* out_buffer, size_t length) {
    char* fname = "assets/log-file-example-rand.bin";

    FILE* fp = fopen(fname, "r");

    fseek(fp, cmd_offset, 0);

    fread(out_buffer, 1, length, fp);
    
    fclose(fp);
}

void kvsb_header_calc_checksum(kvsb_header_t* header) {
    header->header_checsum = MAGIC_NUMBER;
    XXH128_hash_t checksum = XXH3_128bits(header, sizeof(header));
    header->header_checsum = checksum;
}

bool kvsb_header_validate_checksum(kvsb_header_t* header) {
    XXH128_hash_t checksum = header->header_checsum;

    header->header_checsum = MAGIC_NUMBER;
    XXH128_hash_t checksum_found = XXH3_128bits(header, sizeof(header));
    header->header_checsum = checksum;

    return XXH128_cmp(&checksum, &checksum_found) == 0;
}