#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../include/log_file.h"
#include "../include/globals.h"


uint64_t LogEntry_len(const uint8_t* log_entry) {
    uint8_t* ptr = (((uint8_t*) log_entry) + 1);

    uint64_t str_len = strlen((char*) ptr);
    ptr += str_len + 1;

    uint64_t orig_ptr = (uint64_t) ptr;
    ptr = jump_to_alignment(ptr, 8);

    int64_t key_padding = ((uint64_t) ptr) - orig_ptr;
    assert(key_padding > 0);

    uint64_t val_len = *((uint64_t*) ptr);
    ptr += (val_len + 8);
    
    uint64_t length_1 = ((uint64_t) ptr) - ((uint64_t) log_entry);
    uint64_t length_2 = 1 + str_len + 1 + 8 + val_len + key_padding;

    assert(length_1 == length_2);
    return length_1;
}

StateCommand LogEntry_get_cmd(const uint8_t* log_entry) {
    return (StateCommand) ((uint8_t*) log_entry)[0];
}

char* LogEntry_get_key(const uint8_t* log_entry) {
    uint8_t* ptr = (((uint8_t*) log_entry) + 1);
    return (char*) ptr;
}

void LogEntry_get_value(const uint8_t* log_entry, uint8_t** value, uint64_t* value_len) {
    uint8_t* ptr = (((uint8_t*) log_entry) + 1);
    ptr += strlen((char*) ptr) + 1;
    ptr = jump_to_alignment(ptr, 8);

    *value_len = *((uint64_t*) ptr);
    *value = ptr + 8;
}

