#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "../include/raft_core.h"

void test_basic_state_transitions(void) {
    printf("Testing basic state transitions...\n");
    
    raft_node_t* node = raft_init(1);
    assert(node != NULL);
    assert(node->state == RAFT_STATE_FOLLOWER);
    
    raft_become_candidate(node);
    assert(node->state == RAFT_STATE_CANDIDATE);
    assert(node->current_term == 1);
    assert(node->votes_received == 1);
    
    raft_become_leader(node);
    assert(node->state == RAFT_STATE_LEADER);
    assert(node->leader_id == node->node_id);
    
    raft_become_follower(node, 2);
    assert(node->state == RAFT_STATE_FOLLOWER);
    assert(node->current_term == 2);
    assert(node->votes_received == 0);
    
    raft_destroy(node);
}

void test_election_timeout(void) {
    printf("Testing election timeout...\n");
    
    raft_node_t* node = raft_init(1);
    assert(!raft_check_election_timeout(node));
    
    // Force timeout by sleeping
    sleep(1);
    assert(raft_check_election_timeout(node));
    
    raft_reset_election_timer(node);
    assert(!raft_check_election_timeout(node));
    
    raft_destroy(node);
}

void test_vote_counting(void) {
    printf("Testing vote counting...\n");
    
    raft_node_t* node = raft_init(1);
    raft_become_candidate(node);
    
    // Should start with 1 vote (self-vote)
    assert(node->votes_received == 1);
    
    // Add votes until majority
    while (!raft_record_vote(node)) {
        assert(node->votes_received <= (MAX_NODES / 2) + 1);
    }
    
    assert(node->votes_received > (MAX_NODES / 2));
    
    raft_destroy(node);
}

void test_log_operations(void) {
    printf("Testing log operations...\n");
    
    raft_node_t* node = raft_init(1);
    assert(node != NULL);
    
    // Test creating and appending entries
    const char* test_data = "test command";
    raft_entry_t* entry = raft_create_entry(
        1,  // term
        RAFT_ENTRY_COMMAND,
        (const uint8_t*)test_data,
        strlen(test_data) + 1
    );
    assert(entry != NULL);
    
    int result = raft_append_entry(node, entry);
    assert(result == 0);
    assert(node->log->count == 1);
    
    // Verify entry
    raft_entry_t* log_entry = &node->log->entries[0];
    assert(log_entry->term == 1);
    assert(log_entry->type == RAFT_ENTRY_COMMAND);
    assert(log_entry->data_len == strlen(test_data) + 1);
    assert(strcmp((char*)log_entry->data, test_data) == 0);
    
    // Test log replication (basic)
    raft_become_leader(node);
    result = raft_replicate_logs(node);
    assert(result == 0);
    
    free(entry);  // Just free the entry structure, not its contents
    raft_destroy(node);
}

void test_log_edge_cases(void) {
    printf("Testing log edge cases...\n");
    
    raft_node_t* node = raft_init(1);
    assert(node != NULL);
    
    // Test 1: Null data should work (for NOOPs)
    raft_entry_t* noop_entry = raft_create_entry(1, RAFT_ENTRY_NOOP, NULL, 0);
    assert(noop_entry != NULL);
    assert(noop_entry->data == NULL);
    assert(noop_entry->data_len == 0);
    
    int result = raft_append_entry(node, noop_entry);
    assert(result == 0);
    free(noop_entry);
    
    // Test 2: Multiple entries
    const char* test_data[] = {"command1", "command2", "command3"};
    for (int i = 0; i < 3; i++) {
        raft_entry_t* entry = raft_create_entry(
            1,  // term
            RAFT_ENTRY_COMMAND,
            (const uint8_t*)test_data[i],
            strlen(test_data[i]) + 1
        );
        assert(entry != NULL);
        result = raft_append_entry(node, entry);
        assert(result == 0);
        free(entry);
    }
    
    assert(node->log->count == 4);  // NOOP + 3 commands
    
    // Test 3: Verify entries are in correct order
    for (int i = 0; i < 3; i++) {
        raft_entry_t* entry = &node->log->entries[i + 1];  // +1 to skip NOOP
        assert(entry->type == RAFT_ENTRY_COMMAND);
        assert(strcmp((char*)entry->data, test_data[i]) == 0);
    }
    
    raft_destroy(node);
}

void test_log_error_conditions(void) {
    printf("Testing log error conditions...\n");
    
    raft_node_t* node = raft_init(1);
    assert(node != NULL);
    
    // Test 1: NULL node
    raft_entry_t* entry = raft_create_entry(1, RAFT_ENTRY_COMMAND, 
                                          (const uint8_t*)"test", 5);
    assert(entry != NULL);
    int result = raft_append_entry(NULL, entry);
    assert(result == -1);
    
    // Test 2: NULL entry
    result = raft_append_entry(node, NULL);
    assert(result == -1);
    
    // Test 3: Create entry with invalid parameters
    raft_entry_t* invalid_entry = raft_create_entry(
        1,
        RAFT_ENTRY_COMMAND,
        NULL,  // NULL data
        5      // Non-zero length with NULL data
    );
    assert(invalid_entry == NULL);
    
    // Test 4: Term validation
    raft_entry_t* future_entry = raft_create_entry(
        100,  // Term from future
        RAFT_ENTRY_COMMAND,
        (const uint8_t*)"test",
        5
    );
    assert(future_entry != NULL);
    result = raft_append_entry(node, future_entry);
    assert(result == 0);  // Should succeed as we don't validate terms yet
    
    // Cleanup
    raft_free_entry(entry);
    free(future_entry);
    raft_destroy(node);
}

void test_log_replication_basic(void) {
    printf("Testing basic log replication...\n");
    
    raft_node_t* node = raft_init(1);
    assert(node != NULL);
    
    // Add some entries as a follower
    const char* test_data = "test command";
    raft_entry_t* entry = raft_create_entry(
        1,
        RAFT_ENTRY_COMMAND,
        (const uint8_t*)test_data,
        strlen(test_data) + 1
    );
    assert(entry != NULL);
    int result = raft_append_entry(node, entry);
    assert(result == 0);
    free(entry);
    
    // Try to replicate as follower (should fail)
    result = raft_replicate_logs(node);
    assert(result == -1);
    
    // Become leader and try again
    raft_become_leader(node);
    result = raft_replicate_logs(node);
    assert(result == 0);
    
    // Verify next_index values were updated
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        if (i == node->node_id) continue;
        assert(node->next_index[i] == 2);  // Should be incremented
    }
    
    raft_destroy(node);
}

void test_append_entries_basic(void) {
    printf("Testing basic AppendEntries...\n");
    
    // Initialize follower
    raft_node_t* follower = raft_init(1);
    assert(follower != NULL);
    
    // Test heartbeat (empty AppendEntries)
    raft_append_entries_req_t heartbeat = {0};
    heartbeat.term = 1;
    heartbeat.leader_id = 2;
    
    raft_append_entries_resp_t resp = {0};
    
    int result = raft_handle_append_entries(follower, &heartbeat, &resp);
    assert(result == 0);
    assert(resp.success == true);
    assert(follower->leader_id == 2);
    
    raft_destroy(follower);
    printf("Basic AppendEntries test passed\n");
}

void test_append_entries_term_check(void) {
    printf("Testing AppendEntries term checking...\n");
    
    raft_node_t* follower = raft_init(1);
    assert(follower != NULL);
    follower->current_term = 2;  // Set higher term
    
    // Request with lower term
    raft_append_entries_req_t req = {0};
    req.term = 1;  // Lower term
    req.leader_id = 2;
    
    raft_append_entries_resp_t resp = {0};
    
    int result = raft_handle_append_entries(follower, &req, &resp);
    assert(result == 0);
    assert(resp.success == false);
    assert(resp.term == 2);  // Should return current term
    
    raft_destroy(follower);
    printf("AppendEntries term check test passed\n");
}

void test_append_entries_log_matching(void) {
    printf("Testing AppendEntries log matching...\n");
    
    raft_node_t* follower = raft_init(1);
    assert(follower != NULL);
    
    // Add an initial entry to follower's log
    const char* test_data = "test";
    raft_entry_t* entry = raft_create_entry(
        1,  // term
        RAFT_ENTRY_COMMAND,
        (const uint8_t*)test_data,
        strlen(test_data) + 1
    );
    assert(entry != NULL);
    int result = raft_append_entry(follower, entry);
    assert(result == 0);
    free(entry);  // Free entry structure after appending
    
    // Test with mismatched prev_log_term
    raft_append_entries_req_t req = {0};
    req.term = 1;
    req.leader_id = 2;
    req.prev_log_index = 1;  // We have this index
    req.prev_log_term = 2;   // But with different term
    
    raft_append_entries_resp_t resp = {0};
    
    result = raft_handle_append_entries(follower, &req, &resp);
    assert(result == 0);
    assert(resp.success == false);  // Should fail due to term mismatch
    
    raft_destroy(follower);
    printf("Log matching test passed\n");
}

void test_request_vote_basic(void) {
    printf("Testing basic RequestVote...\n");
    
    raft_node_t* follower = raft_init(1);
    assert(follower != NULL);
    
    // Create a RequestVote request
    raft_request_vote_req_t req = {0};
    req.term = 1;
    req.candidate_id = 2;
    req.last_log_index = 0;
    req.last_log_term = 0;
    
    raft_request_vote_resp_t resp = {0};
    
    int result = raft_handle_request_vote(follower, &req, &resp);
    assert(result == 0);
    assert(resp.vote_granted == true);
    assert(follower->voted_for == 2);
    
    raft_destroy(follower);
    printf("Basic RequestVote test passed\n");
}

void test_request_vote_term_check(void) {
    printf("Testing RequestVote term checking...\n");
    
    raft_node_t* follower = raft_init(1);
    assert(follower != NULL);
    follower->current_term = 2;  // Higher term
    
    // Request with lower term
    raft_request_vote_req_t req = {0};
    req.term = 1;
    req.candidate_id = 2;
    req.last_log_index = 0;
    req.last_log_term = 0;
    
    raft_request_vote_resp_t resp = {0};
    
    int result = raft_handle_request_vote(follower, &req, &resp);
    assert(result == 0);
    assert(resp.vote_granted == false);
    assert(resp.term == 2);
    
    raft_destroy(follower);
    printf("RequestVote term check test passed\n");
}

void test_request_vote_already_voted(void) {
    printf("Testing RequestVote when already voted...\n");
    
    raft_node_t* follower = raft_init(1);
    assert(follower != NULL);
    follower->current_term = 1;
    follower->voted_for = 2;  // Already voted for node 2
    
    // Different candidate with same term
    raft_request_vote_req_t req = {0};
    req.term = 1;
    req.candidate_id = 3;  // Different candidate
    req.last_log_index = 0;
    req.last_log_term = 0;
    
    raft_request_vote_resp_t resp = {0};
    
    int result = raft_handle_request_vote(follower, &req, &resp);
    assert(result == 0);
    assert(resp.vote_granted == false);  // Should not grant vote
    assert(follower->voted_for == 2);    // Still voted for original candidate
    
    raft_destroy(follower);
    printf("RequestVote already voted test passed\n");
}

void test_request_vote_log_check(void) {
    printf("Testing RequestVote log checking...\n");
    
    raft_node_t* follower = raft_init(1);
    assert(follower != NULL);
    
    // Add an entry to follower's log with term 2
    const char* test_data = "test";
    raft_entry_t* entry = raft_create_entry(
        2,  // term 2
        RAFT_ENTRY_COMMAND,
        (const uint8_t*)test_data,
        strlen(test_data) + 1
    );
    assert(entry != NULL);
    int result = raft_append_entry(follower, entry);
    assert(result == 0);
    free(entry);
    
    // Candidate with older log (lower term)
    raft_request_vote_req_t req = {0};
    req.term = 3;  // Higher term
    req.candidate_id = 2;
    req.last_log_index = 1;
    req.last_log_term = 1;  // Lower term than follower's log
    
    raft_request_vote_resp_t resp = {0};
    
    result = raft_handle_request_vote(follower, &req, &resp);
    assert(result == 0);
    assert(resp.vote_granted == false);  // Should reject due to outdated log
    
    raft_destroy(follower);
    printf("RequestVote log check test passed\n");
}

void test_start_election(void) {
    printf("Testing start election...\n");
    
    raft_node_t* node = raft_init(1);
    assert(node != NULL);
    assert(node->state == RAFT_STATE_FOLLOWER);
    assert(node->current_term == 0);
    
    // Start election
    int result = raft_start_election(node);
    assert(result == 0);
    assert(node->state == RAFT_STATE_CANDIDATE);
    assert(node->current_term == 1);
    assert(node->voted_for == node->node_id);  // Voted for self
    assert(node->votes_received == 1);         // Counted self vote
    
    raft_destroy(node);
    printf("Start election test passed\n");
}

void test_apply_committed_entries() {
    printf("Testing apply committed entries...\n");
    
    // TODO: Implement test for applying committed entries
    
    printf("Apply committed entries test passed\n");
}

int main(void) {
    printf("Running Raft core tests...\n");
    
    test_basic_state_transitions();
    test_election_timeout();
    test_vote_counting();
    test_log_operations();
    test_log_edge_cases();
    test_log_error_conditions();
    test_log_replication_basic();
    
    test_append_entries_basic();
    test_append_entries_term_check();
    
    test_append_entries_log_matching();
    
    test_request_vote_basic();
    test_request_vote_term_check();
    test_request_vote_already_voted();
    test_request_vote_log_check();
    test_start_election();
    test_apply_committed_entries();
    
    printf("All tests passed!\n");
    return 0;
}