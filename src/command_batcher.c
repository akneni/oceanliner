#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "../include/command_batcher.h"

// Helper function to get current time in milliseconds
uint64_t get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

// Helper function to get elapsed time in milliseconds
static uint64_t get_elapsed_ms(struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    uint64_t start_ms = (uint64_t)start->tv_sec * 1000 + (uint64_t)start->tv_nsec / 1000000;
    uint64_t now_ms = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    
    return now_ms - start_ms;
}

// Batch processing thread function
static void* batch_processor_thread(void *arg) {
    CommandBatcher *batcher = (CommandBatcher*) arg;
    
    while (batcher->active) {
        bool should_flush = false;
        
        // Check if we need to flush the current batch
        pthread_mutex_lock(&batcher->batch_mutex);
        
        if (batcher->current_batch.num_commands > 0) {
            uint64_t elapsed = get_elapsed_ms(&batcher->current_batch.creation_time);
            
            // Flush if batch delay exceeded or batch is full
            if (elapsed >= batcher->batch_delay_ms || 
                batcher->current_batch.total_length >= MAX_COMMAND_BUFFER / 2) {
                should_flush = true;
            }
        }
        
        // If should flush, process the batch and create a new one
        if (should_flush) {
            // Create a copy of the batch for processing
            CommandBatch batch_copy = batcher->current_batch;
            
            // Create a new batch
            command_batcher_new_batch(batcher);
            
            pthread_mutex_unlock(&batcher->batch_mutex);
            
            // Process the batch (call the callback)
            if (batcher->process_batch_callback) {
                batcher->process_batch_callback(&batch_copy, batcher->callback_context);
            }
        } else {
            pthread_mutex_unlock(&batcher->batch_mutex);
            
            // Sleep for a short time before checking again
            usleep(10000); // 10ms
        }
    }
    
    return NULL;
}

int command_batcher_init(CommandBatcher *batcher, uint32_t batch_delay_ms,
                        batch_callback_t process_batch_callback,
                        void* callback_context) {
    if (!batcher) {
        return -1;
    }
    
    // Initialize the batcher
    memset(batcher, 0, sizeof(CommandBatcher));
    batcher->batch_delay_ms = batch_delay_ms > 0 ? batch_delay_ms : DEFAULT_BATCH_DELAY_MS;
    batcher->process_batch_callback = process_batch_callback;
    batcher->callback_context = callback_context;
    batcher->active = false;
    
    // Initialize mutex
    if (pthread_mutex_init(&batcher->batch_mutex, NULL) != 0) {
        return -1;
    }
    
    // Initialize the first batch
    command_batcher_new_batch(batcher);
    
    return 0;
}

void command_batcher_destroy(CommandBatcher *batcher) {
    if (!batcher) {
        return;
    }
    
    // Stop the batcher if it's running
    if (batcher->active) {
        command_batcher_stop(batcher);
    }
    
    // Destroy the mutex
    pthread_mutex_destroy(&batcher->batch_mutex);
}

int command_batcher_start(CommandBatcher *batcher) {
    if (!batcher || batcher->active) {
        return -1;
    }
    
    batcher->active = true;
    
    // Start the batch processor thread
    if (pthread_create(&batcher->batch_thread, NULL, batch_processor_thread, batcher) != 0) {
        batcher->active = false;
        return -1;
    }
    
    return 0;
}

int command_batcher_stop(CommandBatcher *batcher) {
    if (!batcher || !batcher->active) {
        return -1;
    }
    
    // Set active to false to stop the thread
    batcher->active = false;
    
    // Wait for the thread to exit
    pthread_join(batcher->batch_thread, NULL);
    
    // Process any remaining commands in the batch
    pthread_mutex_lock(&batcher->batch_mutex);
    if (batcher->current_batch.num_commands > 0 && batcher->process_batch_callback) {
        CommandBatch batch_copy = batcher->current_batch;
        command_batcher_new_batch(batcher);
        pthread_mutex_unlock(&batcher->batch_mutex);
        
        batcher->process_batch_callback(&batch_copy, batcher->callback_context);
    } else {
        pthread_mutex_unlock(&batcher->batch_mutex);
    }
    
    return 0;
}

bool command_batcher_add_command(CommandBatcher *batcher, const uint8_t *command, 
                               uint32_t command_length) {
    if (!batcher || !command || command_length == 0) {
        return false;
    }
    
    pthread_mutex_lock(&batcher->batch_mutex);
    
    // Check if the command would fit in the batch
    if (batcher->current_batch.total_length + command_length > MAX_COMMAND_BUFFER) {
        // Batch would overflow, unlock and return false
        pthread_mutex_unlock(&batcher->batch_mutex);
        return false;
    }
    
    // Copy the command data to the batch buffer
    memcpy(&batcher->current_batch.commands[batcher->current_batch.total_length],
           command, command_length);
    
    // Update batch metrics
    batcher->current_batch.total_length += command_length;
    batcher->current_batch.num_commands++;
    
    pthread_mutex_unlock(&batcher->batch_mutex);
    return true;
}

void command_batcher_flush(CommandBatcher *batcher) {
    if (!batcher) {
        return;
    }
    
    pthread_mutex_lock(&batcher->batch_mutex);
    
    // Only flush if there are commands in the batch
    if (batcher->current_batch.num_commands > 0 && batcher->process_batch_callback) {
        // Create a copy of the batch for processing
        CommandBatch batch_copy = batcher->current_batch;
        
        // Create a new batch
        command_batcher_new_batch(batcher);
        
        pthread_mutex_unlock(&batcher->batch_mutex);
        
        // Process the batch (call the callback)
        batcher->process_batch_callback(&batch_copy, batcher->callback_context);
    } else {
        pthread_mutex_unlock(&batcher->batch_mutex);
    }
}

void command_batcher_new_batch(CommandBatcher *batcher) {
    if (!batcher) {
        return;
    }
    
    // Initialize a new empty batch
    memset(&batcher->current_batch, 0, sizeof(CommandBatch));
    
    // Set the creation time
    clock_gettime(CLOCK_MONOTONIC, &batcher->current_batch.creation_time);
}