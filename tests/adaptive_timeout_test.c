#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <inttypes.h>
#include "../include/adaptive_timeout.h"

// Helper function to sleep for a specific number of milliseconds
static void msleep(int milliseconds) {
    usleep(milliseconds * 1000);
}

// Test initialization with default configuration
void test_init_default() {
    AdaptiveTimeout timeout;
    int result = adaptive_timeout_init(&timeout);
    assert(result == 0);
    assert(timeout.estimated_rtt == INITIAL_TIMEOUT_MS);
    assert(timeout.rtt_dev == 0);
    assert(timeout.current_timeout == INITIAL_TIMEOUT_MS);
    assert(timeout.total_rpcs == 0);
    assert(timeout.missed_heartbeats == 0);
    assert(timeout.min_rtt == 0);
    assert(timeout.max_rtt == 0);
    assert(timeout.avg_rtt == 0);
    
    adaptive_timeout_destroy(&timeout);
    printf("✓ test_init_default passed\n");
}

// Test initialization with custom configuration
void test_init_custom() {
    AdaptiveTimeout timeout;
    AdaptiveTimeoutConfig config = {
        .alpha = 0.1,
        .beta = 0.2,
        .safety_factor = 2.0,
        .min_timeout = 100,
        .max_timeout = 400,
        .initial_timeout = 200
    };
    
    int result = adaptive_timeout_init_with_config(&timeout, &config);
    assert(result == 0);
    assert(timeout.config.alpha == 0.1);
    assert(timeout.config.beta == 0.2);
    assert(timeout.config.safety_factor == 2.0);
    assert(timeout.config.min_timeout == 100);
    assert(timeout.config.max_timeout == 400);
    assert(timeout.config.initial_timeout == 200);
    
    adaptive_timeout_destroy(&timeout);
    printf("✓ test_init_custom passed\n");
}

// Test RTT measurement and timeout adjustment
void test_rtt_measurement() {
    AdaptiveTimeout timeout;
    adaptive_timeout_init(&timeout);
    
    // Simulate RPC with 100ms RTT
    adaptive_timeout_rpc_start(&timeout);
    msleep(100);
    adaptive_timeout_rpc_end(&timeout);
    
    // Check statistics
    uint64_t total_rpcs;
    double min_rtt, max_rtt, avg_rtt;
    adaptive_timeout_get_stats(&timeout, &total_rpcs, NULL, &min_rtt, &max_rtt, &avg_rtt);
    
    // Print actual values for debugging
    printf("RTT Statistics:\n");
    printf("  Total RPCs: %" PRIu64 "\n", total_rpcs);
    printf("  Min RTT: %.2f ms\n", min_rtt);
    printf("  Max RTT: %.2f ms\n", max_rtt);
    printf("  Avg RTT: %.2f ms\n", avg_rtt);
    
    // More lenient timing assertions (allowing for system scheduling)
    assert(total_rpcs == 1);
    assert(min_rtt >= 80 && min_rtt <= 120);  // Allow more timing variance
    assert(max_rtt >= 80 && max_rtt <= 120);  // Should be same as min_rtt for single RPC
    assert(avg_rtt >= 80 && avg_rtt <= 120);
    
    adaptive_timeout_destroy(&timeout);
    printf("✓ test_rtt_measurement passed\n");
}

// Test timeout bounds
void test_timeout_bounds() {
    AdaptiveTimeout timeout;
    AdaptiveTimeoutConfig config = {
        .alpha = 0.1,
        .beta = 0.2,
        .safety_factor = 2.0,
        .min_timeout = 100,
        .max_timeout = 400,
        .initial_timeout = 200
    };
    
    adaptive_timeout_init_with_config(&timeout, &config);
    
    // Test minimum bound
    adaptive_timeout_record_heartbeat(&timeout);
    assert(timeout.current_timeout == config.min_timeout);
    
    // Test maximum bound
    for (int i = 0; i < 10; i++) {
        adaptive_timeout_check_missed_heartbeat(&timeout);
    }
    assert(timeout.current_timeout <= config.max_timeout);
    
    adaptive_timeout_destroy(&timeout);
    printf("✓ test_timeout_bounds passed\n");
}

// Test reset functionality
void test_reset() {
    AdaptiveTimeout timeout;
    adaptive_timeout_init(&timeout);
    
    // Make some changes
    adaptive_timeout_rpc_start(&timeout);
    msleep(100);
    adaptive_timeout_rpc_end(&timeout);
    adaptive_timeout_check_missed_heartbeat(&timeout);
    
    // Reset
    adaptive_timeout_reset(&timeout);
    
    // Check if everything is reset
    assert(timeout.estimated_rtt == INITIAL_TIMEOUT_MS);
    assert(timeout.rtt_dev == 0);
    assert(timeout.current_timeout == INITIAL_TIMEOUT_MS);
    assert(timeout.total_rpcs == 0);
    assert(timeout.missed_heartbeats == 0);
    assert(timeout.min_rtt == 0);
    assert(timeout.max_rtt == 0);
    assert(timeout.avg_rtt == 0);
    
    adaptive_timeout_destroy(&timeout);
    printf("✓ test_reset passed\n");
}

// Test multiple RPCs and statistics
void test_multiple_rpcs() {
    AdaptiveTimeout timeout;
    adaptive_timeout_init(&timeout);
    
    // Simulate multiple RPCs with different RTTs
    for (int i = 0; i < 5; i++) {
        adaptive_timeout_rpc_start(&timeout);
        msleep(100 + i * 20);  // RTTs: 100ms, 120ms, 140ms, 160ms, 180ms
        adaptive_timeout_rpc_end(&timeout);
    }
    
    // Check statistics
    uint64_t total_rpcs;
    double min_rtt, max_rtt, avg_rtt;
    adaptive_timeout_get_stats(&timeout, &total_rpcs, NULL, &min_rtt, &max_rtt, &avg_rtt);
    
    // Print actual values for debugging
    printf("Multiple RPCs Statistics:\n");
    printf("  Total RPCs: %" PRIu64 "\n", total_rpcs);
    printf("  Min RTT: %.2f ms\n", min_rtt);
    printf("  Max RTT: %.2f ms\n", max_rtt);
    printf("  Avg RTT: %.2f ms\n", avg_rtt);
    
    assert(total_rpcs == 5);
    assert(min_rtt >= 80 && min_rtt <= 120);   // First RPC
    assert(max_rtt >= 160 && max_rtt <= 200);  // Last RPC
    assert(avg_rtt >= 120 && avg_rtt <= 160);  // Average of all RPCs
    
    adaptive_timeout_destroy(&timeout);
    printf("✓ test_multiple_rpcs passed\n");
}

// Test missed heartbeats
void test_missed_heartbeats() {
    AdaptiveTimeout timeout;
    adaptive_timeout_init(&timeout);
    
    // Record some missed heartbeats
    for (int i = 0; i < 3; i++) {
        adaptive_timeout_check_missed_heartbeat(&timeout);
    }
    
    // Check statistics
    uint64_t missed_heartbeats;
    adaptive_timeout_get_stats(&timeout, NULL, &missed_heartbeats, NULL, NULL, NULL);
    assert(missed_heartbeats == 3);
    
    adaptive_timeout_destroy(&timeout);
    printf("✓ test_missed_heartbeats passed\n");
}

int main() {
    printf("Running adaptive timeout tests...\n\n");
    
    test_init_default();
    test_init_custom();
    test_rtt_measurement();
    test_timeout_bounds();
    test_reset();
    test_multiple_rpcs();
    test_missed_heartbeats();
    
    printf("\nAll tests passed!\n");
    return 0;
}
