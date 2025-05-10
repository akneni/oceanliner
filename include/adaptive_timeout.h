#ifndef ADAPTIVE_TIMEOUT_H
#define ADAPTIVE_TIMEOUT_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// Default constants for adaptive timeout calculation
#define ALPHA 0.125        // Weight for RTT estimation (EWMA)
#define BETA 0.25         // Weight for RTT deviation
#define SAFETY_FACTOR 1.5 // Safety factor for timeout calculation
#define MIN_TIMEOUT_MS 150 // Minimum election timeout in milliseconds
#define MAX_TIMEOUT_MS 300 // Maximum election timeout in milliseconds
#define INITIAL_TIMEOUT_MS 200 // Initial timeout value

// Configuration structure for adaptive timeout
typedef struct {
    double alpha;           // EWMA weight for RTT
    double beta;           // EWMA weight for RTT deviation
    double safety_factor;  // Safety factor for timeout calculation
    uint32_t min_timeout;  // Minimum timeout in ms
    uint32_t max_timeout;  // Maximum timeout in ms
    uint32_t initial_timeout; // Initial timeout value
} AdaptiveTimeoutConfig;

// Structure to hold adaptive timeout state
typedef struct {
    double estimated_rtt;        // Estimated round-trip time using EWMA
    double rtt_dev;             // RTT deviation for variance calculation
    uint32_t current_timeout;   // Current election timeout in ms
    struct timespec last_request; // For RTT measurement
    pthread_mutex_t mutex;      // Mutex for thread safety
    
    // Statistics tracking
    uint64_t total_rpcs;        // Total number of RPCs measured
    uint64_t missed_heartbeats; // Number of missed heartbeats
    double min_rtt;             // Minimum RTT observed
    double max_rtt;             // Maximum RTT observed
    double avg_rtt;             // Average RTT
    
    // Configuration
    AdaptiveTimeoutConfig config;
} AdaptiveTimeout;

// Initialize the adaptive timeout structure with default configuration
int adaptive_timeout_init(AdaptiveTimeout *timeout);

// Initialize the adaptive timeout structure with custom configuration
int adaptive_timeout_init_with_config(AdaptiveTimeout *timeout, const AdaptiveTimeoutConfig *config);

// Clean up resources
void adaptive_timeout_destroy(AdaptiveTimeout *timeout);

// Reset the timeout to initial state
void adaptive_timeout_reset(AdaptiveTimeout *timeout);

// Start RTT measurement for an RPC
void adaptive_timeout_rpc_start(AdaptiveTimeout *timeout);

// End RTT measurement and update timeout
void adaptive_timeout_rpc_end(AdaptiveTimeout *timeout);

// Record a heartbeat and update timeout
void adaptive_timeout_record_heartbeat(AdaptiveTimeout *timeout);

// Record a vote and update timeout
void adaptive_timeout_record_vote(AdaptiveTimeout *timeout);

// Check for missed heartbeats and update timeout
void adaptive_timeout_check_missed_heartbeat(AdaptiveTimeout *timeout);

// Get the current timeout value
uint32_t adaptive_timeout_get_current(AdaptiveTimeout *timeout);

// Get statistics about the timeout system
void adaptive_timeout_get_stats(AdaptiveTimeout *timeout, 
                              uint64_t *total_rpcs,
                              uint64_t *missed_heartbeats,
                              double *min_rtt,
                              double *max_rtt,
                              double *avg_rtt);

#endif // ADAPTIVE_TIMEOUT_H 