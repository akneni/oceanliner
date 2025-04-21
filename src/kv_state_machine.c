#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/kv_state_machine.h"

// Apply a command to the key-value store
int kv_state_machine_apply(void* ctx, const kvs_command_t* cmd, uint8_t** result, size_t* result_size) {
    if (!ctx || !cmd) return -1;
    
    kv_store_t* kv_store = (kv_store_t*)ctx;
    int apply_result = 0;
    
    // Process based on command type
    switch(cmd->type) {
        case CMD_SET: {
            // Extract key and value
            char* key = kvs_command_get_key(cmd);
            uint8_t* value;
            kvs_command_get_value((kvs_command_t*)cmd, &value);
            
            // For SET operations, we need the command byte offset in the log
            // In a real implementation, this should be passed in or calculated
            // For now, we'll use a placeholder approach
            uint64_t cmd_byte_offset = (uint64_t)((uintptr_t)cmd); // This is just an example
            
            // Apply to key-value store
            apply_result = kv_store_set(kv_store, cmd_byte_offset, key, value, cmd->value_length);
            
            // No result for SET operations
            *result = NULL;
            *result_size = 0;
            break;
        }
        case CMD_DELETE: {
            char* key = kvs_command_get_key(cmd);
            apply_result = kv_store_delete(kv_store, key);
            
            // No result for DELETE operations
            *result = NULL;
            *result_size = 0;
            break;
        }
        case CMD_GET: {
            char* key = kvs_command_get_key(cmd);
            int64_t value_len = 0;
            
            // Get from key-value store
            uint8_t* value = kv_store_get(kv_store, key, &value_len);
            
            if (value_len >= 0 && value != NULL) {
                // Successfully got value
                *result = value;  // Transfer ownership to caller
                *result_size = value_len;
            } else {
                // Key not found or error
                *result = NULL;
                *result_size = 0;
                apply_result = -1;
            }
            break;
        }
        default:
            fprintf(stderr, "Unknown command type: %d\n", cmd->type);
            apply_result = -2;
            *result = NULL;
            *result_size = 0;
            break;
    }
    
    return apply_result;
}

// Create a key-value store state machine
raft_state_machine_t* kv_state_machine_create(kv_store_t* kv_store) {
    if (!kv_store) return NULL;
    
    raft_state_machine_t* state_machine = malloc(sizeof(raft_state_machine_t));
    if (!state_machine) return NULL;
    
    state_machine->ctx = kv_store;
    state_machine->apply = kv_state_machine_apply;
    
    return state_machine;
}

// Free resources used by a key-value store state machine
void kv_state_machine_destroy(raft_state_machine_t* state_machine) {
    // Just free the state machine structure
    // Note: This does not free the key-value store itself
    free(state_machine);
}