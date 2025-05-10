#ifndef RAFT_CORE_ADAPTIVE_H
#define RAFT_CORE_ADAPTIVE_H

#include "raft_core.h"
#include "adaptive_timeout.h"

// Initialize adaptive timeout for a Raft node
int raft_adaptive_init(raft_node_t *node);

// Clean up adaptive timeout resources
void raft_adaptive_destroy(raft_node_t *node);

// Start RTT measurement for an RPC
void raft_adaptive_rpc_start(raft_node_t *node);

// End RTT measurement and update timeout
void raft_adaptive_rpc_end(raft_node_t *node);

// Record a heartbeat and update timeout
void raft_adaptive_record_heartbeat(raft_node_t *node);

// Record a vote and update timeout
void raft_adaptive_record_vote(raft_node_t *node);

// Check for missed heartbeats and update timeout
void raft_adaptive_check_missed_heartbeat(raft_node_t *node);

// Get the current timeout value
uint32_t raft_adaptive_get_timeout(raft_node_t *node);

#endif // RAFT_CORE_ADAPTIVE_H 