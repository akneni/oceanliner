#include "../include/raft_core_adaptive.h"
#include <stdlib.h>
#include <string.h>

// Initialize the adaptive timeout system for a Raft node
// This is called when we create a new Raft node - it sets up the timeout tracking
int raft_adaptive_init(raft_node_t *node) {
    if (!node) {
        return -1;
    }
    
    // Allocate and initialize adaptive timeout structure
    // We need to allocate this dynamically since it's part of the node structure
    node->adaptive_timeout = (AdaptiveTimeout*)malloc(sizeof(AdaptiveTimeout));
    if (!node->adaptive_timeout) {
        return -1;
    }
    
    // Initialize with default configuration
    // The defaults should work well for most networks
    return adaptive_timeout_init(node->adaptive_timeout);
}

// Clean up the adaptive timeout system
// Make sure to call this when destroying a Raft node to prevent memory leaks
void raft_adaptive_destroy(raft_node_t *node) {
    if (!node || !node->adaptive_timeout) {
        return;
    }
    
    // First destroy the timeout system, then free the memory
    adaptive_timeout_destroy(node->adaptive_timeout);
    free(node->adaptive_timeout);
    node->adaptive_timeout = NULL;
}

// Start timing an RPC
// Call this right before sending any RPC (AppendEntries or RequestVote)
void raft_adaptive_rpc_start(raft_node_t *node) {
    if (!node || !node->adaptive_timeout) {
        return;
    }
    
    adaptive_timeout_rpc_start(node->adaptive_timeout);
}

// End timing an RPC
// Call this as soon as we get a response to our RPC
void raft_adaptive_rpc_end(raft_node_t *node) {
    if (!node || !node->adaptive_timeout) {
        return;
    }
    
    adaptive_timeout_rpc_end(node->adaptive_timeout);
}

// Record a heartbeat
// This is called when we receive a valid AppendEntries RPC
// It helps us track network stability
void raft_adaptive_record_heartbeat(raft_node_t *node) {
    if (!node || !node->adaptive_timeout) {
        return;
    }
    
    adaptive_timeout_record_heartbeat(node->adaptive_timeout);
}

// Record a vote
// Called when we grant a vote to another node
// Similar to heartbeat, helps us track network conditions
void raft_adaptive_record_vote(raft_node_t *node) {
    if (!node || !node->adaptive_timeout) {
        return;
    }
    
    adaptive_timeout_record_vote(node->adaptive_timeout);
}

// Check for missed heartbeat
// This is called periodically to see if we need to start an election
// It's a key part of the leader election mechanism
void raft_adaptive_check_missed_heartbeat(raft_node_t *node) {
    if (!node || !node->adaptive_timeout) {
        return;
    }
    
    adaptive_timeout_check_missed_heartbeat(node->adaptive_timeout);
}

// Get current timeout value
// This is what the Raft protocol uses to decide when to start elections
// Returns INITIAL_TIMEOUT_MS if something's wrong with the timeout system
uint32_t raft_adaptive_get_timeout(raft_node_t *node) {
    if (!node || !node->adaptive_timeout) {
        return INITIAL_TIMEOUT_MS;
    }
    
    return adaptive_timeout_get_current(node->adaptive_timeout);
} 