#include "../include/adaptive_timeout.h"
#include <time.h>
#include <math.h>
#include <stdio.h>

// Helper function to initialize with configuration
// This is where we set up all the initial values for our timeout system
// We use a separate function to avoid code duplication between default and custom init
static int init_with_config(AdaptiveTimeout *timeout, const AdaptiveTimeoutConfig *config) {
    if (!timeout) return -1;
    
    // Initialize with configuration values
    // These are the core values we'll use to calculate timeouts
    timeout->estimated_rtt = config->initial_timeout;
    timeout->rtt_dev = 0;
    timeout->current_timeout = config->initial_timeout;
    
    // Initialize statistics
    // We start with zeros for RTT stats since we haven't measured anything yet
    timeout->total_rpcs = 0;
    timeout->missed_heartbeats = 0;
    timeout->min_rtt = 0;  // Start at 0 instead of initial_timeout to properly track first RTT
    timeout->max_rtt = 0;  // Same here - we want to capture the actual first RTT
    timeout->avg_rtt = 0;  // And here - we'll calculate the real average as we go
    
    // Copy configuration
    // Store the config so we can reference it later for min/max bounds
    timeout->config = *config;
    
    // Set up mutex for thread safety
    // This is crucial since we're dealing with a distributed system
    if (pthread_mutex_init(&timeout->mutex, NULL) != 0) {
        return -1;
    }
    
    return 0;
}

// Initialize with default configuration
// This is what most users will use - it sets up reasonable defaults
int adaptive_timeout_init(AdaptiveTimeout *timeout) {
    if (!timeout) return -1;
    
    // Create default configuration
    // These values are based on typical network conditions
    AdaptiveTimeoutConfig default_config = {
        .alpha = ALPHA,           // 0.125 - makes the system responsive but not too jumpy
        .beta = BETA,             // 0.25 - helps smooth out RTT variations
        .safety_factor = SAFETY_FACTOR,  // 2.0 - adds some buffer for network jitter
        .min_timeout = MIN_TIMEOUT_MS,   // 150ms - minimum reasonable timeout
        .max_timeout = MAX_TIMEOUT_MS,   // 300ms - maximum timeout to prevent too long waits
        .initial_timeout = INITIAL_TIMEOUT_MS  // 200ms - good starting point
    };
    
    return init_with_config(timeout, &default_config);
}

// Initialize with custom configuration
// For users who need to tune the system for their specific network
int adaptive_timeout_init_with_config(AdaptiveTimeout *timeout, const AdaptiveTimeoutConfig *config) {
    if (!timeout || !config) return -1;
    return init_with_config(timeout, config);
}

// Clean up resources
// Don't forget to call this when you're done with the timeout system
void adaptive_timeout_destroy(AdaptiveTimeout *timeout) {
    if (!timeout) return;
    pthread_mutex_destroy(&timeout->mutex);
}

// Reset the timeout system to initial state
// Useful when you want to start fresh without creating a new instance
void adaptive_timeout_reset(AdaptiveTimeout *timeout) {
    if (!timeout) return;
    
    pthread_mutex_lock(&timeout->mutex);
    
    // Reset to initial values
    // This is like creating a new instance but keeping the same config
    timeout->estimated_rtt = timeout->config.initial_timeout;
    timeout->rtt_dev = 0;
    timeout->current_timeout = timeout->config.initial_timeout;
    
    // Reset statistics
    // Clear all our measurements to start fresh
    timeout->total_rpcs = 0;
    timeout->missed_heartbeats = 0;
    timeout->min_rtt = 0;
    timeout->max_rtt = 0;
    timeout->avg_rtt = 0;
    
    pthread_mutex_unlock(&timeout->mutex);
}

// Start timing an RPC
// Call this right before sending the RPC
void adaptive_timeout_rpc_start(AdaptiveTimeout *timeout) {
    if (!timeout) return;
    pthread_mutex_lock(&timeout->mutex);
    clock_gettime(CLOCK_MONOTONIC, &timeout->last_request);
    pthread_mutex_unlock(&timeout->mutex);
}

// End timing an RPC and update statistics
// This is where the magic happens - we calculate RTT and adjust timeouts
void adaptive_timeout_rpc_end(AdaptiveTimeout *timeout) {
    if (!timeout) return;
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    pthread_mutex_lock(&timeout->mutex);
    
    // Calculate RTT in milliseconds
    // This gives us the actual round-trip time for this RPC
    double rtt = (now.tv_sec - timeout->last_request.tv_sec) * 1000.0 +
                 (now.tv_nsec - timeout->last_request.tv_nsec) / 1000000.0;
    
    // Update statistics
    // Keep track of min/max/avg RTT for monitoring and debugging
    timeout->total_rpcs++;
    if (timeout->min_rtt == 0 || rtt < timeout->min_rtt) timeout->min_rtt = rtt;
    if (rtt > timeout->max_rtt) timeout->max_rtt = rtt;
    timeout->avg_rtt = ((timeout->avg_rtt * (timeout->total_rpcs - 1)) + rtt) / timeout->total_rpcs;
    
    // Update estimated RTT using EWMA
    // This is the core of the Chandra-Toueg approach
    double err = rtt - timeout->estimated_rtt;
    timeout->estimated_rtt += timeout->config.alpha * err;
    
    // Update RTT deviation
    // This helps us handle network jitter
    timeout->rtt_dev += timeout->config.beta * (fabs(err) - timeout->rtt_dev);
    
    // Calculate new timeout
    // The formula: timeout = estimated_rtt + safety_factor * rtt_dev
    uint32_t new_timeout = timeout->estimated_rtt + 
                          timeout->config.safety_factor * timeout->rtt_dev;
    
    // Ensure timeout is within bounds
    // Don't let it get too short or too long
    if (new_timeout < timeout->config.min_timeout) new_timeout = timeout->config.min_timeout;
    if (new_timeout > timeout->config.max_timeout) new_timeout = timeout->config.max_timeout;
    
    timeout->current_timeout = new_timeout;
    
    #ifdef DEBUG
    printf("RTT: %.2f ms, Estimated RTT: %.2f ms, RTT Dev: %.2f ms, New Timeout: %u ms\n",
           rtt, timeout->estimated_rtt, timeout->rtt_dev, new_timeout);
    #endif
    
    pthread_mutex_unlock(&timeout->mutex);
}

// Record a heartbeat
// This resets the timeout to minimum when we get a heartbeat
void adaptive_timeout_record_heartbeat(AdaptiveTimeout *timeout) {
    if (!timeout) return;
    pthread_mutex_lock(&timeout->mutex);
    timeout->current_timeout = timeout->config.min_timeout;
    pthread_mutex_unlock(&timeout->mutex);
}

// Record a vote
// Similar to heartbeat - reset timeout when we get a vote
void adaptive_timeout_record_vote(AdaptiveTimeout *timeout) {
    if (!timeout) return;
    pthread_mutex_lock(&timeout->mutex);
    timeout->current_timeout = timeout->config.min_timeout;
    pthread_mutex_unlock(&timeout->mutex);
}

// Check for missed heartbeat
// This increases the timeout when we miss a heartbeat
void adaptive_timeout_check_missed_heartbeat(AdaptiveTimeout *timeout) {
    if (!timeout) return;
    pthread_mutex_lock(&timeout->mutex);
    // Increase timeout when heartbeat is missed
    timeout->missed_heartbeats++;
    timeout->current_timeout = (uint32_t)(timeout->current_timeout * 1.5);
    if (timeout->current_timeout > timeout->config.max_timeout) {
        timeout->current_timeout = timeout->config.max_timeout;
    }
    
    #ifdef DEBUG
    printf("Missed heartbeat detected. New timeout: %u ms\n", timeout->current_timeout);
    #endif
    
    pthread_mutex_unlock(&timeout->mutex);
}

// Get current timeout value
// This is what the Raft protocol uses to check if it's time for an election
uint32_t adaptive_timeout_get_current(AdaptiveTimeout *timeout) {
    if (!timeout) return INITIAL_TIMEOUT_MS;
    
    uint32_t current;
    pthread_mutex_lock(&timeout->mutex);
    current = timeout->current_timeout;
    pthread_mutex_unlock(&timeout->mutex);
    return current;
}

// Get statistics
// Useful for monitoring and debugging the timeout system
void adaptive_timeout_get_stats(AdaptiveTimeout *timeout, 
                              uint64_t *total_rpcs,
                              uint64_t *missed_heartbeats,
                              double *min_rtt,
                              double *max_rtt,
                              double *avg_rtt) {
    if (!timeout) return;
    
    pthread_mutex_lock(&timeout->mutex);
    
    if (total_rpcs) *total_rpcs = timeout->total_rpcs;
    if (missed_heartbeats) *missed_heartbeats = timeout->missed_heartbeats;
    if (min_rtt) *min_rtt = timeout->min_rtt;
    if (max_rtt) *max_rtt = timeout->max_rtt;
    if (avg_rtt) *avg_rtt = timeout->avg_rtt;
    
    pthread_mutex_unlock(&timeout->mutex);
} 