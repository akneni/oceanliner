#include "../include/raft_core.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


// Key-value pair structure
typedef struct {
    char* key;
    uint8_t* value;
    size_t value_size;
} kv_pair_t;

// Key-value store structure
typedef struct {
    kv_pair_t* entries;
    size_t capacity;
    size_t count;
    pthread_mutex_t mutex;
} kv_store_t;

// Create a new key-value store
kv_store_t* kv_store_create(size_t initial_capacity) {
    kv_store_t* store = malloc(sizeof(kv_store_t));
    if (!store) return NULL;
    
    store->entries = malloc(initial_capacity * sizeof(kv_pair_t));
    if (!store->entries) {
        free(store);
        return NULL;
    }
    
    store->capacity = initial_capacity;
    store->count = 0;
    
    if (pthread_mutex_init(&store->mutex, NULL) != 0) {
        free(store->entries);
        free(store);
        return NULL;
    }
    
    return store;
}

/* Will implement the rest of the key-value store functions after
   integrating with HashMap implementation.
   These functions will include:
   - kv_store_destroy
   - kv_store_get
   - kv_store_put
   - kv_store_delete
*/

// State machine apply function
static int kv_store_apply(void* ctx, const raft_command_t* cmd,
                         uint8_t** result, size_t* result_size) {
    kv_store_t* store = (kv_store_t*)ctx;
    if (!store || !cmd) return -1;
    
    pthread_mutex_lock(&store->mutex);
    
    int ret = 0;
    
    switch (cmd->type) {
        case RAFT_CMD_PUT:
            /* Need to integrate with HashMap implementation.
               Will handle PUT operations once I have the working HashMap. */
            break;
            
        case RAFT_CMD_GET:
            /* Will implement after HashMap integration.
               This will retrieve values from the store. */
            break;
            
        case RAFT_CMD_DELETE:
            /* Will implement after HashMap integration.
               This will remove key-value pairs from the store. */
            break;
            
        case RAFT_CMD_NOOP:
            // No operation, just success
            break;
            
        default:
            ret = -1;
            break;
    }
    
    pthread_mutex_unlock(&store->mutex);
    return ret;
}

// Create a Raft state machine using this key-value store
raft_state_machine_t* raft_kv_store_create() {
    // Create the KV store
    kv_store_t* store = kv_store_create(100);  // Initial capacity of 100
    if (!store) return NULL;
    
    // Create the state machine
    raft_state_machine_t* sm = malloc(sizeof(raft_state_machine_t));
    if (!sm) {
        /* Need to implement kv_store_destroy - will complete
           after integrating with HashMap implementation */
        return NULL;
    }
    
    sm->apply = kv_store_apply;
    sm->ctx = store;
    
    return sm;
}

// Destroy the KV store state machine
void raft_kv_store_destroy(raft_state_machine_t* sm) {
    if (!sm) return;
    
    kv_store_t* store = (kv_store_t*)sm->ctx;
    if (store) {
        /* Will implement complete cleanup after
           integrating with HashMap implementation */
    }
    
    free(sm);
}