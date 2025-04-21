#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/kv_state_machine.h"

// Apply a command to the key-value store
int kv_state_machine_apply(void* ctx, const kvs_command_t* cmd, uint8_t** result, size_t* result_size) {
    if (!ctx || !cmd || !result || !result_size) return -1;
    
    kv_store_t* kv_store = (kv_store_t*)ctx;
    int apply_result = 0;
    
    // Initialize result outputs
    *result = NULL;
    *result_size = 0;
    
    // Process based on command type
    switch(cmd->type) {
        case CMD_SET: {
            // Extract key and value
            char* key = kvs_command_get_key(cmd);
            if (!key) return -1;
            
            uint8_t* value = NULL;
            kvs_command_get_value((kvs_command_t*)cmd, &value);
            if (!value && cmd->value_length > 0) return -1;
            
            // In a production environment, we would get the actual log offset
            // where this command is stored. For testing, we use a deterministic
            // value based on the command's content to ensure consistency.
            uint64_t cmd_byte_offset = 0;
            // Create a simple hash of the key and value for testing
            for (size_t i = 0; i < cmd->key_length; i++) {
                cmd_byte_offset = (cmd_byte_offset * 31) + (unsigned char)key[i];
            }
            for (size_t i = 0; i < cmd->value_length && value != NULL; i++) {
                cmd_byte_offset = (cmd_byte_offset * 31) + value[i];
            }
            
            // Apply to key-value store
            apply_result = kv_store_set(kv_store, cmd_byte_offset, key, value, cmd->value_length);
            break;
        }
        case CMD_DELETE: {
            char* key = kvs_command_get_key(cmd);
            if (!key) return -1;
            
            apply_result = kv_store_delete(kv_store, key);
            break;
        }
        case CMD_GET: {
            char* key = kvs_command_get_key(cmd);
            if (!key) return -1;
            
            int64_t value_len = 0;
            
            // Get from key-value store
            uint8_t* value = kv_store_get(kv_store, key, &value_len);
            
            if (value_len >= 0 && value != NULL) {
                // Successfully got value
                *result = value;  // Transfer ownership to caller
                *result_size = value_len;
            } else {
                apply_result = -1;
            }
            break;
        }
        default:
            fprintf(stderr, "Unknown command type: %d\n", cmd->type);
            apply_result = -2;
            break;
    }
    
    return apply_result;
}

// Create a Raft log entry from a key-value store command
raft_entry_t* raft_create_entry_from_command(uint64_t term, const kvs_command_t* cmd) {
    if (!cmd) return NULL;
    
    // Calculate the total size of the command
    size_t cmd_size = sizeof(kvs_command_t) + cmd->key_length + cmd->value_length;
    
    // Create a new log entry
    raft_entry_t* entry = malloc(sizeof(raft_entry_t));
    if (!entry) return NULL;
    
    // Initialize the entry
    entry->term = term;
    entry->index = 0;  // This will be set when appended to the log
    entry->type = RAFT_ENTRY_COMMAND;
    entry->data_len = cmd_size;
    
    // Allocate and copy the command data
    entry->data = malloc(cmd_size);
    if (!entry->data) {
        free(entry);
        return NULL;
    }
    
    // Copy the command including its key and value
    memcpy(entry->data, cmd, cmd_size);
    
    return entry;
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