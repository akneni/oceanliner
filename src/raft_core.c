#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/raft_core.h"
#include "../include/kv_state_machine.h"
#include "../include/command_batcher.h"
#include "../include/raft_core_adaptive.h"

#define MILLISECONDS_PER_SECOND 1000
#define INITIAL_LOG_CAPACITY 1024

// Forward declarations
static raft_log_t* log_init(void);
static void log_destroy(raft_log_t* log);
static void* apply_thread_func(void* arg);
static int apply_command_batch(raft_node_t* node, const uint8_t* batch_data, size_t batch_size);
static void process_raft_batch(CommandBatch* batch, void* context);

static int generate_timeout(void) {
    return ELECTION_TIMEOUT_MIN + 
           (rand() % (ELECTION_TIMEOUT_MAX - ELECTION_TIMEOUT_MIN));
}

static raft_log_t* log_init(void) {
    raft_log_t* log = malloc(sizeof(raft_log_t));
    if (!log) return NULL;
    
    log->entries = malloc(sizeof(raft_entry_t) * INITIAL_LOG_CAPACITY);
    if (!log->entries) {
        free(log);
        return NULL;
    }
    
    log->capacity = INITIAL_LOG_CAPACITY;
    log->count = 0;
    return log;
}

static void log_destroy(raft_log_t* log) {
    if (!log) return;
    
    for (size_t i = 0; i < log->count; i++) {
        free(log->entries[i].data);
    }
    free(log->entries);
    free(log);
}

raft_node_t* raft_init(uint32_t node_id) {
    raft_node_t* node = malloc(sizeof(raft_node_t));
    if (!node) return NULL;

    memset(node, 0, sizeof(raft_node_t));
    
    node->log = log_init();
    if (!node->log) {
        free(node);
        return NULL;
    }
    
    node->node_id = node_id;
    node->state = RAFT_STATE_FOLLOWER;
    node->current_term = 0;
    node->voted_for = 0;
    node->votes_received = 0;
    node->leader_id = 0;
    node->commit_index = 0;
    node->last_applied = 0;
    
    for (int i = 0; i < MAX_NODES; i++) {
        node->next_index[i] = 1;
        node->match_index[i] = 0;
    }
    
    // Initialize adaptive timeout
    if (raft_adaptive_init(node) != 0) {
        log_destroy(node->log);
        free(node);
        return NULL;
    }
    
    node->last_heartbeat = time(NULL);
    
    if (pthread_mutex_init(&node->mutex, NULL) != 0) {
        raft_adaptive_destroy(node);
        log_destroy(node->log);
        free(node);
        return NULL;
    }
    
    // Initialize state machine related fields
    node->state_machine = NULL;  // Set separately
    node->applying_entries = false;
    
    // Initialize command batcher
    command_batcher_init(&node->command_batcher, DEFAULT_BATCH_DELAY_MS, 
                        process_raft_batch, node);
    
    return node;
}

void raft_destroy(raft_node_t* node) {
    if (!node) return;
    
    // Stop apply thread if running
    if (node->applying_entries) {
        raft_stop_apply_thread(node);
    }
    
    // State machine is destroyed separately
    
    // Stop command batcher
    command_batcher_stop(&node->command_batcher);
    command_batcher_destroy(&node->command_batcher);
    
    // Clean up adaptive timeout
    raft_adaptive_destroy(node);
    
    log_destroy(node->log);
    pthread_mutex_destroy(&node->mutex);
    free(node);
}

void raft_become_follower(raft_node_t* node, uint64_t term) {
    if (!node) return;
    
    pthread_mutex_lock(&node->mutex);
    
    // Additional code for handling command batcher
    if (node->state == RAFT_STATE_LEADER) {
        command_batcher_stop(&node->command_batcher);
    }
    
    node->state = RAFT_STATE_FOLLOWER;
    node->current_term = term;
    node->voted_for = 0;
    node->votes_received = 0;
    node->leader_id = 0;
    
    // Record heartbeat for adaptive timeout
    raft_adaptive_record_heartbeat(node);
    node->last_heartbeat = time(NULL);
    
    pthread_mutex_unlock(&node->mutex);
}

void raft_become_candidate(raft_node_t* node) {
    if (!node) return;
    
    pthread_mutex_lock(&node->mutex);
    node->state = RAFT_STATE_CANDIDATE;
    node->current_term++;
    node->voted_for = node->node_id;
    node->votes_received = 1;
    node->leader_id = 0;
    
    // Record vote for adaptive timeout
    raft_adaptive_record_vote(node);
    node->last_heartbeat = time(NULL);
    
    pthread_mutex_unlock(&node->mutex);
}

void raft_become_leader(raft_node_t* node) {
    if (!node) return;
    
    pthread_mutex_lock(&node->mutex);
    node->state = RAFT_STATE_LEADER;
    node->leader_id = node->node_id;
    
    for (int i = 0; i < MAX_NODES; i++) {
        node->next_index[i] = node->log->count + 1;
        node->match_index[i] = 0;
    }
    
    // Start the command batcher when becoming leader
    command_batcher_start(&node->command_batcher);
    
    pthread_mutex_unlock(&node->mutex);
}

bool raft_check_election_timeout(raft_node_t* node) {
    if (!node) return false;
    
    pthread_mutex_lock(&node->mutex);
    bool timeout = false;
    
    if (node->state != RAFT_STATE_LEADER) {
        time_t now = time(NULL);
        time_t elapsed = now - node->last_heartbeat;
        uint32_t current_timeout = raft_adaptive_get_timeout(node);
        
        if (elapsed >= current_timeout / MILLISECONDS_PER_SECOND) {
            timeout = true;
            raft_adaptive_check_missed_heartbeat(node);
        }
    }
    
    pthread_mutex_unlock(&node->mutex);
    return timeout;
}

void raft_reset_election_timer(raft_node_t* node) {
    if (!node) return;
    
    // Record a heartbeat in the adaptive timeout system
    raft_adaptive_record_heartbeat(node);
    
    // Update last heartbeat time
    node->last_heartbeat = time(NULL);
}

bool raft_record_vote(raft_node_t* node) {
    if (!node || node->state != RAFT_STATE_CANDIDATE) return false;
    
    pthread_mutex_lock(&node->mutex);
    node->votes_received++;
    bool has_majority = node->votes_received > (MAX_NODES / 2);
    pthread_mutex_unlock(&node->mutex);
    
    return has_majority;
}

raft_entry_t* raft_create_entry(uint64_t term, raft_entry_type_t type,
                               const uint8_t* data, size_t data_len) {
    if (data == NULL && data_len > 0) return NULL;
    
    raft_entry_t* entry = malloc(sizeof(raft_entry_t));
    if (!entry) return NULL;
    
    entry->term = term;
    entry->type = type;
    entry->data_len = data_len;
    entry->data = NULL;
    
    if (data_len > 0) {
        entry->data = malloc(data_len);
        if (!entry->data) {
            free(entry);
            return NULL;
        }
        memcpy(entry->data, data, data_len);
    }
    
    return entry;
}

void raft_free_entry(raft_entry_t* entry) {
    if (!entry) return;
    free(entry->data);
    free(entry);
}

int raft_append_entry(raft_node_t* node, raft_entry_t* entry) {
    if (!node || !entry) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    if (node->log->count >= node->log->capacity) {
        size_t new_capacity = node->log->capacity * 2;
        raft_entry_t* new_entries = realloc(node->log->entries, 
                                          new_capacity * sizeof(raft_entry_t));
        if (!new_entries) {
            pthread_mutex_unlock(&node->mutex);
            return -1;
        }
        node->log->entries = new_entries;
        node->log->capacity = new_capacity;
    }
    
    entry->index = node->log->count + 1;
    node->log->entries[node->log->count] = *entry;
    node->log->count++;
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

int raft_replicate_logs(raft_node_t* node) {
    if (!node || node->state != RAFT_STATE_LEADER) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        if (i == node->node_id) continue;
        
        uint64_t next_idx = node->next_index[i];
        if (next_idx <= node->log->count) {
            node->next_index[i]++;
        }
    }
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

// AppendEntries RPC handler
int raft_handle_append_entries(raft_node_t* node,
                             const raft_msg_t* req,
                             raft_msg_t* resp) {
    if (!node || !req || !resp) return -1;
    assert(req->rpc_type == RAFT_RPC_APPEND_ENTRIES);

    pthread_mutex_lock(&node->mutex);

    // Initialize response
    resp->term = node->current_term;
    
    resp->rpc_type = RAFT_RPC_RESP_APPEND_ENTRIES;
    raft_append_entries_resp_t* resp_data = (raft_append_entries_resp_t*) resp->data;
    raft_append_entries_req_t* req_data = (raft_append_entries_req_t*) req->data;

    resp_data->success = false;
    
    // 1. Reply false if term < currentTerm
    if (req->term < node->current_term) {
        pthread_mutex_unlock(&node->mutex);
        return 0;
    }
    
    // 2. Update term if needed
    if (req->term > node->current_term) {
        node->current_term = req->term;
        node->voted_for = 0;
        node->state = RAFT_STATE_FOLLOWER;
    }    
    // 3. Reset election timeout
    node->last_heartbeat = time(NULL);
    node->leader_id = req->sender_node_id;
    
    // 4. Check log consistency
    if (req_data->prev_log_index > 0) {
        // Check if we have enough entries
        if (req_data->prev_log_index > node->log->count) {
            pthread_mutex_unlock(&node->mutex);
            return 0; // Not enough entries
        }
        
        // Check term matches
        if (node->log->entries[req_data->prev_log_index - 1].term != req_data->prev_log_term) {
            pthread_mutex_unlock(&node->mutex);
            return 0; // Term mismatch
        }
    }
    
    // 5. Success - log is consistent
    resp_data->success = true;
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

// RequestVote RPC handler
int raft_handle_request_vote(raft_node_t* node,
                           const raft_msg_t* req,
                           raft_msg_t* resp) {
    if (!node || !req || !resp) return -1;
    
    pthread_mutex_lock(&node->mutex);

    resp->rpc_type = RAFT_RPC_RESP_REQUEST_VOTE;
    raft_request_vote_req_t* req_data = (raft_request_vote_req_t*) req->data;
    raft_request_vote_resp_t* resp_data = (raft_request_vote_resp_t*) resp->data;


    // Initialize response
    resp->term = node->current_term;
    resp_data->vote_granted = false;
    
    // 1. Reply false if term < currentTerm
    if (req->term < node->current_term) {
        pthread_mutex_unlock(&node->mutex);
        return 0;
    }
    
    // 2. If term > currentTerm, update currentTerm
    if (req->term > node->current_term) {
        node->current_term = req->term;
        node->state = RAFT_STATE_FOLLOWER;
        node->voted_for = 0;
    }
    
    // 3. If votedFor is null or candidateId, and candidate's log is
    // at least as up-to-date as receiver's log, grant vote
    bool log_ok = false;
    
    // Get last log info
    uint64_t last_idx = node->log->count;
    uint64_t last_term = (last_idx > 0) ? 
                        node->log->entries[last_idx - 1].term : 0;
    
    // Check if candidate's log is at least as up-to-date as ours
    if (req_data->last_log_term > last_term) {
        log_ok = true;  // Candidate has higher term
    } else if (req_data->last_log_term == last_term && 
            req_data->last_log_index >= last_idx) {
        log_ok = true;  // Same term but equal or longer log
    }
    
    if ((node->voted_for == 0 || node->voted_for == req->sender_node_id) && log_ok) {
        resp_data->vote_granted = true;
        node->voted_for = req->sender_node_id;
        
        // Reset election timer since we just voted
        node->last_heartbeat = time(NULL);
    }
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

// Send AppendEntries RPC
int raft_send_append_entries(raft_node_t* node,
                           uint32_t target_id,
                           raft_entry_t* entries,
                           size_t entries_len) {
    if (!node || !entries) return -1;
    
    // Start RTT measurement
    raft_adaptive_rpc_start(node);
    
    pthread_mutex_lock(&node->mutex);
    
    size_t msg_body_len = entries_len + 2*sizeof(kvsb_header_t);

    raft_msg_t* req = (raft_msg_t*) malloc(msg_body_len + sizeof(raft_msg_t));


    *req = (raft_msg_t) {
        .term = node->current_term,
        .sender_node_id = node->node_id,
        .rpc_type = RAFT_RPC_APPEND_ENTRIES,
        .body_length = (entries_len + 2*sizeof(kvsb_header_t)),
    };

    raft_append_entries_req_t* req_data = (raft_append_entries_req_t*) req->data;
    req_data->prev_log_term = (node->current_term == 0) ? 0 : node->current_term - 1;
    req_data->prev_log_index = (uint64_t) node->next_index - 1;
    req_data->leader_commit = node->commit_index;


    kvsb_header_t header = {
        .data_length = entries_len,
        .log_index = (uint64_t) node->next_index,
        .term = node->current_term,
    };
    kvsb_header_calc_checksum(&header);

    req_data->entries.header = header;
    *((kvsb_header_t*) (&req_data->entries.data[entries_len])) = header;

    memcpy(req_data->entries.data, entries, entries_len);
    

    // Note: Actual network send would happen here
    // For now, we just update our local state
    node->next_index[target_id] += entries_len;
    
    pthread_mutex_unlock(&node->mutex);
    
    // End RTT measurement
    raft_adaptive_rpc_end(node);
    
    return 0;
}

// Send RequestVote RPC
int raft_send_request_vote(raft_node_t* node,
                          uint32_t target_id) {
    if (!node) return -1;
    
    // Start RTT measurement
    raft_adaptive_rpc_start(node);
    
    pthread_mutex_lock(&node->mutex);
    
    raft_msg_t* req = (raft_msg_t*) malloc(sizeof(raft_msg_t) + sizeof(raft_request_vote_req_t));
    *req = (raft_msg_t) {
        .term = node->current_term,
        .body_length = sizeof(raft_request_vote_req_t),
        .rpc_type = RAFT_RPC_REQUEST_VOTE,
        .sender_node_id = node->node_id,
    };

    raft_request_vote_req_t* req_data = (raft_request_vote_req_t*) req;
    req_data->last_log_index = node->log->count;
    req_data->last_log_term = (node->log->count > 0) ? node->log->entries[node->log->count - 1].term : 0;
    
    // Note: Actual network send would happen here
    
    pthread_mutex_unlock(&node->mutex);
    
    // End RTT measurement
    raft_adaptive_rpc_end(node);
    
    return 0;
}

// Start an election (becomes candidate and requests votes)
int raft_start_election(raft_node_t* node) {
    if (!node) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    // Become candidate
    node->state = RAFT_STATE_CANDIDATE;
    node->current_term++;
    node->voted_for = node->node_id;  // Vote for self
    node->votes_received = 1;         // Count self vote
    node->last_heartbeat = time(NULL);  // Reset election timer
    
    // Prepare RequestVote request
    // raft_request_vote_req_t req = {0};
    // req.term = node->current_term;
    // req.candidate_id = node->node_id;
    // req.last_log_index = node->log->count;
    // req.last_log_term = (req.last_log_index > 0) ? 
    //                   node->log->entries[req.last_log_index - 1].term : 0;
    // ^^ this code is re-reimplementing the logic in `raft_send_request_vote()`. We probably don't need to do this twice
    
    pthread_mutex_unlock(&node->mutex);
    
    // Would send RequestVote to all other nodes here
    // For now, we just simulate a successful election
    
    return 0;
}

// Apply committed entries to state machine
int raft_apply_committed_entries(raft_node_t* node) {
    if (!node || !node->state_machine) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    // Check if there are entries to apply
    if (node->last_applied >= node->commit_index) {
        pthread_mutex_unlock(&node->mutex);
        return 0;  // Nothing to apply
    }
    
    // Apply all committed but not yet applied entries
    for (uint64_t i = node->last_applied + 1; i <= node->commit_index; i++) {
        if (i > node->log->count) break;  // Safety check
        
        raft_entry_t* entry = &node->log->entries[i - 1];
        
        // Skip entries that aren't commands
        if (entry->type != RAFT_ENTRY_COMMAND) {
            node->last_applied = i;
            continue;
        }
        
        // Apply to state machine
        int apply_result = 0;
        
        const uint8_t* data = entry->data;
        
        // Check if this is a batch entry
        if (entry->data_len > sizeof(kvs_command_t)) {
            // Detect if this is a batch by checking if there appears to be
            // multiple commands serialized together
            const kvs_command_t* first_cmd = (const kvs_command_t*)data;
            size_t first_cmd_len = kvs_command_len(first_cmd);
            
            if (first_cmd_len < entry->data_len) {
                // This looks like a batch, apply as batch
                apply_result = apply_command_batch(
                    node,
                    data,
                    entry->data_len
                );
            } else {
                // Apply as single command
                uint8_t* result = NULL;
                size_t result_size = 0;
                
                apply_result = node->state_machine->apply(
                    node->state_machine->ctx,
                    (kvs_command_t*)data,
                    &result,
                    &result_size
                );
                
                // Free result if it was allocated
                if (result) {
                    free(result);
                }
            }
        } else {
            // Apply as single command
            uint8_t* result = NULL;
            size_t result_size = 0;
            
            apply_result = node->state_machine->apply(
                node->state_machine->ctx,
                (kvs_command_t*)data,
                &result,
                &result_size
            );
            
            // Free result if it was allocated
            if (result) {
                free(result);
            }
        }
        
        // Handle apply result
        if (apply_result != 0) {
            fprintf(stderr, "Failed to apply command at index %lu, error: %d\n", 
                    i, apply_result);
        }
        
        // Update last applied
        node->last_applied = i;
    }
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

// Apply thread function
static void* apply_thread_func(void* arg) {
    raft_node_t* node = (raft_node_t*)arg;
    
    while (node->applying_entries) {
        raft_apply_committed_entries(node);
        usleep(10000);  // Sleep 10ms between apply attempts
    }
    
    return NULL;
}

// Start background apply thread
int raft_start_apply_thread(raft_node_t* node) {
    if (!node) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    if (node->applying_entries) {
        pthread_mutex_unlock(&node->mutex);
        return 0;  // Already running
    }
    
    node->applying_entries = true;
    
    int result = pthread_create(&node->apply_thread, NULL, apply_thread_func, node);
    if (result != 0) {
        node->applying_entries = false;
        pthread_mutex_unlock(&node->mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

// Stop background apply thread
int raft_stop_apply_thread(raft_node_t* node) {
    if (!node) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    if (!node->applying_entries) {
        pthread_mutex_unlock(&node->mutex);
        return 0;  // Not running
    }
    
    node->applying_entries = false;
    pthread_mutex_unlock(&node->mutex);
    
    pthread_join(node->apply_thread, NULL);
    return 0;
}

// Initialize the state machine with a key-value store
int raft_init_state_machine(raft_node_t* node, kv_store_t* kv_store) {
    if (!node || !kv_store) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    // Create state machine
    raft_state_machine_t* state_machine = kv_state_machine_create(kv_store);
    if (!state_machine) {
        pthread_mutex_unlock(&node->mutex);
        return -1;
    }
    
    // Free any existing state machine
    if (node->state_machine) {
        kv_state_machine_destroy(node->state_machine);
    }
    
    // Set the new state machine
    node->state_machine = state_machine;
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

// Add this new function for batch processing
static void process_raft_batch(CommandBatch* batch, void* context) {
    raft_node_t* node = (raft_node_t*)context;
    
    // Create a new log entry with the batch
    raft_entry_t* entry = raft_create_entry(
        node->current_term,
        RAFT_ENTRY_COMMAND,
        batch->commands,
        batch->total_length
    );
    
    // Append to log and start consensus
    raft_append_entry(node, entry);
    
    // If leader, replicate to followers
    if (node->state == RAFT_STATE_LEADER) {
        raft_replicate_logs(node);
    }
    
    // Free the entry (since raft_append_entry makes a copy)
    free(entry->data);
    free(entry);
}

// Add this function to handle client commands
int raft_handle_client_command(raft_node_t* node, 
                             const kvs_command_t* cmd) {
    if (!node || !cmd) return -1;
    
    // Only leaders can process client commands
    if (node->state != RAFT_STATE_LEADER) {
        return -2; // Not leader
    }
    
    // Calculate command size
    size_t cmd_size = kvs_command_len(cmd);
    
    // Add to batch
    if (!command_batcher_add_command(&node->command_batcher, 
                                   (const uint8_t*)cmd, cmd_size)) {
        // If the command is too large for batching, handle individually
        // This is rare but possible for very large commands
        
        // Create a new log entry directly
        raft_entry_t* entry = raft_create_entry(
            node->current_term,
            RAFT_ENTRY_COMMAND,
            (const uint8_t*)cmd,
            cmd_size
        );
        
        // Start consensus for this entry
        raft_append_entry(node, entry);
        raft_replicate_logs(node);
        
        // Free the entry
        free(entry->data);
        free(entry);
    }
    
    return 0;
}

// Function to apply a batch of commands to state machine
static int apply_command_batch(raft_node_t* node, const uint8_t* batch_data, 
                             size_t batch_size) {
    if (!node || !node->state_machine || !batch_data || batch_size == 0) return -1;
    
    size_t offset = 0;
    int apply_result = 0;
    
    // Process all commands in the batch
    while (offset < batch_size) {
        const kvs_command_t* cmd = (const kvs_command_t*)(batch_data + offset);
        
        // Apply each command using the standard apply function
        uint8_t* cmd_result = NULL;
        size_t cmd_result_size = 0;
        
        int cmd_apply_result = node->state_machine->apply(
            node->state_machine->ctx,
            cmd,
            &cmd_result,
            &cmd_result_size
        );
        
        // Free any result from individual command
        if (cmd_result) {
            free(cmd_result);
        }
        
        // Track if any command failed
        if (cmd_apply_result != 0) {
            apply_result = cmd_apply_result;
        }
        
        // Move to next command
        size_t cmd_size = kvs_command_len(cmd);
        offset += cmd_size;
    }
    
    return apply_result;
}

// Add this function to raft_core.c
void raft_check_heartbeat_status(raft_node_t* node) {
    if (!node || node->state != RAFT_STATE_FOLLOWER) return;
    
    // Calculate expected heartbeat interval (usually election_timeout/2)
    uint32_t expected_interval = raft_adaptive_get_timeout(node) / 2;
    time_t expected_seconds = (expected_interval / 1000) + 1;
    
    time_t now = time(NULL);
    if ((now - node->last_heartbeat) > expected_seconds) {
        // We've missed an expected heartbeat
        raft_adaptive_check_missed_heartbeat(node);
        
        #ifdef DEBUG
        printf("Missed heartbeat detected. Time since last: %ld sec\n", 
               now - node->last_heartbeat);
        #endif
    }
}