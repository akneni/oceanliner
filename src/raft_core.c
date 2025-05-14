#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "../include/raft_core.h"
#include "../include/kv_state_machine.h"
#include "../include/command_batcher.h"
#include "../include/log_file.h"

#define MILLISECONDS_PER_SECOND 1000
#define INITIAL_LOG_CAPACITY 1024


static int generate_timeout(void) {
    return ELECTION_TIMEOUT_MIN + 
           (rand() % (ELECTION_TIMEOUT_MAX - ELECTION_TIMEOUT_MIN));
}

raft_node_t raft_init(uint8_t node_id, logfile_t* logfile) {
    raft_node_t node;
    
    node.node_id = node_id;
    node.state = RAFT_STATE_FOLLOWER;
    node.current_term = 0;
    node.voted_for = 0;
    node.votes_received = 0;
    node.leader_id = 0;
    node.commit_index = 0;

    node.last_applied_term = 0;
    node.last_applied_index = 0;
    
    logfile_get_last_tai(logfile, &node.last_applied_term, &node.last_applied_index);


    if (node_id == 0) {
        node.state = RAFT_STATE_LEADER;
    }

    for (int i = 0; i < MAX_NODES; i++) {
        node.next_index[i] = 1;
        node.match_index[i] = 0;
    }
    
    node.election_timeout = generate_timeout();
    node.last_heartbeat = time(NULL);
    
    if (pthread_mutex_init(&node.mutex, NULL) != 0) {
        perror("Failed to initialize mutex");
        exit(1);
    }
    
    // Initialize state machine related fields
    node.state_machine = NULL;  // Set separately
    node.applying_entries = false;
        
    return node;
}

void raft_become_follower(raft_node_t* node, uint64_t term) {
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
    node->leader_id = -1;
    node->election_timeout = generate_timeout();
    node->last_heartbeat = time(NULL);
    pthread_mutex_unlock(&node->mutex);
}

void raft_become_leader(raft_node_t* node) {
    pthread_mutex_lock(&node->mutex);
    node->state = RAFT_STATE_LEADER;
    node->leader_id = node->node_id;
    node->votes_received = 0;
    
    for (int i = 0; i < MAX_NODES; i++) {
        node->match_index[i] = 0;
    }
    
    pthread_mutex_unlock(&node->mutex);
}

/// @brief 
/// @param node 
/// @return Returns true if the timeout has been exceeded. 
bool raft_check_election_timeout(raft_node_t* node) {

    pthread_mutex_lock(&node->mutex);
    time_t now = time(NULL);
    int timeout_seconds = (node->election_timeout / MILLISECONDS_PER_SECOND) + 1;
    bool timeout = (now - node->last_heartbeat) >= timeout_seconds;
    pthread_mutex_unlock(&node->mutex);
    
    return timeout;
}

void raft_reset_election_timer(raft_node_t* node) {
    // No need to lock here as this is called within locked functions
    node->election_timeout = generate_timeout();
    node->last_heartbeat = time(NULL);
}

bool raft_record_vote(raft_node_t* node) {
    assert(node->state == RAFT_STATE_CANDIDATE);
    
    pthread_mutex_lock(&node->mutex);
    node->votes_received++;
    bool has_majority = node->votes_received > (MAX_NODES / 2);
    pthread_mutex_unlock(&node->mutex);
    
    return has_majority;
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
