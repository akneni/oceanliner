#ifndef COMMAND_BATCHER_H
#define COMMAND_BATCHER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

// Maximum size of the command buffer
#define MAX_COMMAND_BUFFER 65536  // 64KB

// Maximum batch delay in milliseconds
#define DEFAULT_BATCH_DELAY_MS 50

// Command batch structure that holds multiple commands for efficient replication
typedef struct CommandBatch {
    uint32_t num_commands;             // Number of commands in the batch
    uint64_t total_length;             // Total length of all commands in bytes
    uint8_t commands[MAX_COMMAND_BUFFER]; // Buffer containing serialized commands
    struct timespec creation_time;     // When the batch was created
} CommandBatch;

// Function pointer type for batch processing callback
typedef void (*batch_callback_t)(CommandBatch* batch, void* context);

// Command batcher context to manage batch creation
typedef struct CommandBatcher {
    CommandBatch current_batch;        // The batch currently being filled
    pthread_mutex_t batch_mutex;       // Mutex to protect batch access
    uint32_t batch_delay_ms;           // Time to wait before forcing a batch (ms)
    bool active;                       // Whether the batcher is active
    pthread_t batch_thread;            // Thread for batch processing
    batch_callback_t process_batch_callback; // Callback when batch is ready
    void* callback_context;            // Context to pass to callback
} CommandBatcher;

// Initialize a new command batcher
int command_batcher_init(CommandBatcher *batcher, uint32_t batch_delay_ms, 
                         batch_callback_t process_batch_callback,
                         void* callback_context);

// Clean up the command batcher resources
void command_batcher_destroy(CommandBatcher *batcher);

// Start the command batcher (starts the batching thread)
int command_batcher_start(CommandBatcher *batcher);

// Stop the command batcher
int command_batcher_stop(CommandBatcher *batcher);

// Add a command to the current batch
// Returns true if the command was added, false otherwise
bool command_batcher_add_command(CommandBatcher *batcher, const uint8_t *command, 
                                uint32_t command_length);

// Force the current batch to be processed immediately
void command_batcher_flush(CommandBatcher *batcher);

// Create a new empty batch in the batcher
void command_batcher_new_batch(CommandBatcher *batcher);

#endif // COMMAND_BATCHER_H