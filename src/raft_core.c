#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/raft_core.h"

#define MILLISECONDS_PER_SECOND 1000
#define INITIAL_LOG_CAPACITY 1024

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
    
    node->election_timeout = generate_timeout();
    node->last_heartbeat = time(NULL);
    
    if (pthread_mutex_init(&node->mutex, NULL) != 0) {
        log_destroy(node->log);
        free(node);
        return NULL;
    }
    
    // Initialize state machine related fields
    node->state_machine = NULL;  // Set separately
    node->applying_entries = false;
    
    return node;
}

void raft_destroy(raft_node_t* node) {
    if (!node) return;
    
    // Stop apply thread if running
    if (node->applying_entries) {
        raft_stop_apply_thread(node);
    }
    
    // State machine is destroyed separately
    
    log_destroy(node->log);
    pthread_mutex_destroy(&node->mutex);
    free(node);
}

void raft_become_follower(raft_node_t* node, uint64_t term) {
    if (!node) return;
    
    pthread_mutex_lock(&node->mutex);
    node->state = RAFT_STATE_FOLLOWER;
    node->current_term = term;
    node->voted_for = 0;
    node->votes_received = 0;
    node->leader_id = 0;
    node->election_timeout = generate_timeout();
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
    node->election_timeout = generate_timeout();
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
    pthread_mutex_unlock(&node->mutex);
}

bool raft_check_election_timeout(raft_node_t* node) {
    if (!node) return false;
    
    pthread_mutex_lock(&node->mutex);
    time_t now = time(NULL);
    int timeout_seconds = (node->election_timeout / MILLISECONDS_PER_SECOND) + 1;
    bool timeout = (now - node->last_heartbeat) >= timeout_seconds;
    pthread_mutex_unlock(&node->mutex);
    
    return timeout;
}

void raft_reset_election_timer(raft_node_t* node) {
    if (!node) return;
    
    // No need to lock here as this is called within locked functions
    node->election_timeout = generate_timeout();
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
                             const raft_append_entries_req_t* req,
                             raft_append_entries_resp_t* resp) {
    if (!node || !req || !resp) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    // Initialize response
    resp->term = node->current_term;
    resp->success = false;
    
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
    node->leader_id = req->leader_id;
    
    // 4. Check log consistency
    if (req->prev_log_index > 0) {
        // Check if we have enough entries
        if (req->prev_log_index > node->log->count) {
            pthread_mutex_unlock(&node->mutex);
            return 0; // Not enough entries
        }
        
        // Check term matches
        if (node->log->entries[req->prev_log_index - 1].term != req->prev_log_term) {
            pthread_mutex_unlock(&node->mutex);
            return 0; // Term mismatch
        }
    }
    
    // 5. Success - log is consistent
    resp->success = true;
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

// RequestVote RPC handler
int raft_handle_request_vote(raft_node_t* node,
                           const raft_request_vote_req_t* req,
                           raft_request_vote_resp_t* resp) {
    if (!node || !req || !resp) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    // Initialize response
    resp->term = node->current_term;
    resp->vote_granted = false;
    
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
    if (req->last_log_term > last_term) {
        log_ok = true;  // Candidate has higher term
    } else if (req->last_log_term == last_term && 
               req->last_log_index >= last_idx) {
        log_ok = true;  // Same term but equal or longer log
    }
    
    if ((node->voted_for == 0 || node->voted_for == req->candidate_id) && log_ok) {
        resp->vote_granted = true;
        node->voted_for = req->candidate_id;
        
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
                           size_t n_entries) {
    if (!node || node->state != RAFT_STATE_LEADER) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    // Prepare request
    raft_append_entries_req_t req = {
        .term = node->current_term,
        .leader_id = node->node_id,
        .prev_log_index = node->next_index[target_id] - 1,
        .prev_log_term = (req.prev_log_index > 0) ? 
            node->log->entries[req.prev_log_index - 1].term : 0,
        .entries = entries,
        .n_entries = n_entries,
        .leader_commit = node->commit_index
    };
    
    // Note: Actual network send would happen here
    // For now, we just update our local state
    node->next_index[target_id] += n_entries;
    
    pthread_mutex_unlock(&node->mutex);
    return 0;
}

// Send RequestVote RPC
int raft_send_request_vote(raft_node_t* node,
                          uint32_t target_id) {
    if (!node || node->state != RAFT_STATE_CANDIDATE) return -1;
    
    pthread_mutex_lock(&node->mutex);
    
    // Prepare request
    raft_request_vote_req_t req = {
        .term = node->current_term,
        .candidate_id = node->node_id,
        .last_log_index = node->log->count,
        .last_log_term = (node->log->count > 0) ?
            node->log->entries[node->log->count - 1].term : 0
    };
    
    // Note: Actual network send would happen here
    
    pthread_mutex_unlock(&node->mutex);
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
    raft_request_vote_req_t req = {0};
    req.term = node->current_term;
    req.candidate_id = node->node_id;
    req.last_log_index = node->log->count;
    req.last_log_term = (req.last_log_index > 0) ? 
                      node->log->entries[req.last_log_index - 1].term : 0;
    
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
        
        // Convert log entry to command
        // This is where you'd parse the entry data into a command
        kvs_command_t cmd = {0};
        
        // TODO: Implement entry data parsing to command
        
        // Apply to state machine
        uint8_t* result = NULL;
        size_t result_size = 0;
        
        if (node->state_machine->apply(node->state_machine->ctx, &cmd, &result, &result_size) != 0) {
            // Handle apply error
            pthread_mutex_unlock(&node->mutex);
            return -1;
        }
        
        // Free result if needed
        if (result) free(result);
        
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