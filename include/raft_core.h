#ifndef RAFT_CORE_H
#define RAFT_CORE_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>

#include "../include/globals.h"
#include "../include/log_file.h"
#include "../include/xxhash.h"
#include "command_batcher.h"

// Forward declaration of AdaptiveTimeout structure
typedef struct AdaptiveTimeout AdaptiveTimeout;

// Constants
#define MAX_NODES 5
#define ELECTION_TIMEOUT_MIN 150
#define ELECTION_TIMEOUT_MAX 300

// Node states
typedef enum {
    RAFT_STATE_FOLLOWER,
    RAFT_STATE_CANDIDATE,
    RAFT_STATE_LEADER
} raft_state_t;

// Log entry types
typedef enum {
    RAFT_ENTRY_NOOP,    // Used during leader election
    RAFT_ENTRY_COMMAND  // Normal command from client
} raft_entry_type_t;

// Log entry structure
typedef struct {
    uint64_t term;      // Term when entry was received by leader
    uint64_t index;     // Position in the log
    raft_entry_type_t type;
    uint8_t* data;      // Command data
    size_t data_len;    // Length of command data
} raft_entry_t;

// Log structure
typedef struct {
    raft_entry_t* entries;  // Array of log entries
    size_t capacity;        // Total capacity of entries array
    size_t count;          // Number of entries in log
} raft_log_t;

// State machine interface
typedef struct {
    // State machine operations - to be implemented
    int (*apply)(void* ctx, const kvs_command_t* cmd, uint8_t** result, size_t* result_size);
    
    // Implementation-specific context
    void* ctx;
} raft_state_machine_t;

// Node structure
typedef struct {
    // Basic state
    raft_state_t state;
    uint32_t node_id;
    uint64_t current_term;
    uint32_t voted_for;
    
    // Election state
    uint32_t votes_received;
    time_t last_heartbeat;
    
    // Leader state
    uint32_t leader_id;
    
    // Log state
    raft_log_t* log;
    uint64_t commit_index;    // Highest log entry known to be committed
    uint64_t last_applied;    // Highest log entry applied to state machine
    
    // Leader state (for each server)
    uint64_t next_index[MAX_NODES];    // Index of next log entry to send
    uint64_t match_index[MAX_NODES];   // Index of highest log entry known to be replicated
    
    // State machine
    raft_state_machine_t* state_machine;
    bool applying_entries;    // Flag for background apply thread
    pthread_t apply_thread;   // Thread for applying entries
    
    // Thread safety
    pthread_mutex_t mutex;
    
    // Command batching
    CommandBatcher command_batcher;

    // Adaptive timeout
    AdaptiveTimeout* adaptive_timeout;
} raft_node_t;

// AppendEntries RPC structures
typedef struct {
    uint64_t prev_log_index;   // Index of log entry before new ones
    uint64_t prev_log_term;    // Term of prev_log_index entry
    uint64_t leader_commit;    // Leader's commitIndex
    size_t n_entries;          // Number of entries
    kvs_batch_cmd_t entries;   // Log entries to store 
} raft_append_entries_req_t;

typedef struct {
    bool success;        // True if follower matched prev_log_index and prev_log_term

    // These fields are only relevent if `success` is false
    uint64_t lc_log_idx; // last committed log index
    uint64_t lc_term;    // last committed term
} raft_append_entries_resp_t;

// RequestVote RPC structures
typedef struct {
    uint64_t last_log_index;  // Index of candidate's last log entry
    uint64_t last_log_term;   // Term of candidate's last log entry
} raft_request_vote_req_t;

typedef struct {
    bool vote_granted;    // True means candidate received vote
} raft_request_vote_resp_t;

// RPC Message Types
typedef enum {
    RAFT_RPC_APPEND_ENTRIES,
    RAFT_RPC_REQUEST_VOTE,
    RAFT_RPC_RESP_APPEND_ENTRIES,
    RAFT_RPC_RESP_REQUEST_VOTE
} raft_rpc_type_t;

// Wrapper struct of any message sent between nodes in the RSM
typedef struct {
    uint64_t body_length;
    uint64_t term;
    uint64_t sender_node_id;
    raft_rpc_type_t rpc_type;
    uint8_t padding[7];
    uint64_t data[];
} raft_msg_t;

// Core functions
raft_node_t* raft_init(uint32_t node_id);
void raft_destroy(raft_node_t* node);

// State transitions
void raft_become_follower(raft_node_t* node, uint64_t term);
void raft_become_candidate(raft_node_t* node);
void raft_become_leader(raft_node_t* node);

// Election-related functions
bool raft_check_election_timeout(raft_node_t* node);
void raft_reset_election_timer(raft_node_t* node);
bool raft_record_vote(raft_node_t* node);

// Log operations
raft_entry_t* raft_create_entry(uint64_t term, raft_entry_type_t type, 
                               const uint8_t* data, size_t data_len);
void raft_free_entry(raft_entry_t* entry);
int raft_append_entry(raft_node_t* node, raft_entry_t* entry);
int raft_replicate_logs(raft_node_t* node);

// AppendEntries RPC handler
int raft_handle_append_entries(raft_node_t* node,
                             const raft_msg_t* req,
                             raft_msg_t* resp);

// RPC handler functions
int raft_handle_request_vote(raft_node_t* node,
                           const raft_msg_t* req,
                           raft_msg_t* resp);

// RPC sender functions
int raft_send_append_entries(raft_node_t* node,
                           uint32_t target_id,
                           raft_entry_t* entries,
                           size_t n_entries);

int raft_send_request_vote(raft_node_t* node,
                          uint32_t target_id);

int raft_start_election(raft_node_t* node);

// Function to apply committed log entries
int raft_apply_committed_entries(raft_node_t* node);

// Function to start background apply thread
int raft_start_apply_thread(raft_node_t* node);

// Function to stop background apply thread
int raft_stop_apply_thread(raft_node_t* node);

// State machine initialization
int raft_init_state_machine(raft_node_t* node, kv_store_t* kv_store);

// Client command handling
int raft_handle_client_command(raft_node_t* node, const kvs_command_t* cmd);

#endif // RAFT_CORE_H

