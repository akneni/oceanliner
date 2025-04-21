#ifndef KV_STATE_MACHINE_H
#define KV_STATE_MACHINE_H

#include "raft_core.h"
#include "kv_store.h"
#include "log_file.h"

/**
 * @brief Apply a command to the key-value store state machine
 * 
 * @param ctx The key-value store context (kv_store_t*)
 * @param cmd The command to apply
 * @param result Output parameter for result data (for GET operations)
 * @param result_size Output parameter for result size
 * @return int 0 on success, negative value on error
 */
int kv_state_machine_apply(void* ctx, const kvs_command_t* cmd, uint8_t** result, size_t* result_size);

/**
 * @brief Create a key-value store state machine
 * 
 * @param kv_store The key-value store to use
 * @return raft_state_machine_t* New state machine or NULL on error
 */
raft_state_machine_t* kv_state_machine_create(kv_store_t* kv_store);

/**
 * @brief Free resources used by a key-value store state machine
 * 
 * @param state_machine The state machine to free
 */
void kv_state_machine_destroy(raft_state_machine_t* state_machine);

#endif // KV_STATE_MACHINE_H