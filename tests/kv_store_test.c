#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>      // For open
#include <unistd.h>     // For read, close, lseek
#include <sys/stat.h>   // For fstat

#include "../include/globals.h"
// #include "../include/cluster.h" // Likely not needed for this specific test
#include "../include/log_file.h"
#include "../include/kv_store.h"  // Include the header for the system under test

// Helper function to extract command from log entry
// Assumes log_entry points to the start of a valid kvs_command_t structure
kvs_op_t LogEntry_get_cmd(const uint8_t* log_entry) {
    // Cast the buffer pointer to the command structure pointer
    const kvs_command_t* cmd = (const kvs_command_t*)log_entry;
    return cmd->type;
}

// Helper function to extract key from log entry
// Assumes log_entry points to the start of a valid kvs_command_t structure
char* LogEntry_get_key(const uint8_t* log_entry) {
    const kvs_command_t* cmd = (const kvs_command_t*)log_entry;
    // The key starts immediately after the fixed part of the struct (data field)
    return kvs_command_get_key(cmd);
}

// Helper function to extract value and its length from log entry
// Assumes log_entry points to the start of a valid kvs_command_t structure
void LogEntry_get_value(const uint8_t* log_entry, uint8_t** value, uint32_t* value_len) {
    kvs_command_t* cmd = (kvs_command_t*)log_entry;
    *value_len = cmd->value_length;
    // Pass the non-const pointer to the helper function
    kvs_command_get_value(cmd, value);
}

// Helper function to get log entry length
// Assumes log_entry points to the start of a valid kvs_command_t structure
uint64_t LogEntry_len(const uint8_t* log_entry) {
    const kvs_command_t* cmd = (const kvs_command_t*)log_entry;
    // Use the provided function to calculate the total length
    return kvs_command_len(cmd);
}

// Helper function to verify GET results match expected values
void verify_get_value(kv_store_t* map, const char* key, uint8_t* expected_value,
                      size_t expected_len, bool should_exist) {
    int64_t actual_len = 0;
    // Use the updated kv_store_get function
    uint8_t* actual_value = kv_store_get(map, key, &actual_len);

    if (!should_exist) {
        if (actual_len == -1) {
            printf("✓ Key '%s' correctly not found\n", key);
        } else {
            printf("✗ ERROR: Key '%s' was found (len %ld) but shouldn't exist\n", key, actual_len);
            free(actual_value); // Free memory if unexpectedly found
            exit(1);
        }
        // actual_value should be NULL if not found, no need to free
        return;
    }

    // If it should exist, check if it was found
    if (actual_len == -1) {
        printf("✗ ERROR: Key '%s' not found but should exist\n", key);
        exit(1);
    }

    // Check if lengths match
    if ((uint64_t)actual_len != expected_len) {
        printf("✗ ERROR: Value length mismatch for key '%s': expected %zu, got %ld\n",
               key, expected_len, actual_len);
        free(actual_value);
        exit(1);
    }

    // Check if values match byte-by-byte
    for (size_t i = 0; i < expected_len; i++) {
        if (actual_value[i] != expected_value[i]) {
            printf("✗ ERROR: Value mismatch for key '%s' at index %zu: expected %d, got %d\n",
                   key, i, expected_value[i], actual_value[i]);
            // Optional: Print more context around the mismatch
            printf("   Expected: ...");
            for(size_t j = (i > 5 ? i-5 : 0); j < (i+5 < expected_len ? i+5 : expected_len); ++j) printf("%02X ", expected_value[j]);
            printf("...\n");
            printf("   Actual:   ...");
            for(size_t j = (i > 5 ? i-5 : 0); j < (i+5 < (size_t)actual_len ? i+5 : (size_t)actual_len); ++j) printf("%02X ", actual_value[j]);
            printf("...\n");

            free(actual_value);
            exit(1);
        }
    }

    printf("✓ Key '%s' correctly retrieved with matching value (len %ld)\n", key, actual_len);
    free(actual_value); // Free the buffer allocated by kv_store_get
}

// Loads the entire binary log file into memory
// Returns the total size of the log data read, or 0 on error.
// The caller is responsible for freeing the returned buffer.
uint8_t* load_binary_log(const char* filepath, size_t* log_size_out) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        perror("Error opening binary log file");
        *log_size_out = 0;
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("Error getting file size");
        close(fd);
        *log_size_out = 0;
        return NULL;
    }

    size_t file_size = st.st_size;
    if (file_size == 0) {
        fprintf(stderr, "Log file is empty: %s\n", filepath);
        close(fd);
        *log_size_out = 0;
        return NULL; // Or return an empty buffer if that's valid
    }

    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (buffer == NULL) {
        fprintf(stderr, "Failed to allocate memory for log buffer (%zu bytes)\n", file_size);
        close(fd);
        *log_size_out = 0;
        return NULL;
    }

    ssize_t bytes_read = read(fd, buffer, file_size);
    if (bytes_read == -1) {
        perror("Error reading log file");
        free(buffer);
        close(fd);
        *log_size_out = 0;
        return NULL;
    }

    if ((size_t)bytes_read != file_size) {
        fprintf(stderr, "Error: Read partial file (%zd bytes read, %zu expected)\n", bytes_read, file_size);
        free(buffer);
        close(fd);
        *log_size_out = 0;
        return NULL;
    }

    close(fd);
    *log_size_out = file_size;
    printf("Successfully loaded binary log file '%s' (%zu bytes)\n", filepath, file_size);
    return buffer;
}

// Asserts that all page latches in the kv_store are unlocked.
void kv_store_assert_unlocked(const kv_store_t* map) {
    // Calculate the number of pages based on the initialization logic
    uint64_t num_pages = (1 << (map->num_slots_log2 - KVE_NUM_SLOTS_LOG2)) + 1;
    for(uint64_t i = 0; i < num_pages -1 ; i++) { // Iterate up to num_pages-1 as in init
        // Get a mutable pointer to the page to access the mutex
        kvs_page_t* page = &map->data[i];

        int res = pthread_mutex_trylock(&page->latch);
        if (res == 0) {
            // Successfully locked, so unlock it immediately
            pthread_mutex_unlock(&page->latch);
        } else {
            // Failed to lock, means it was already locked
            fprintf(stderr, "✗ ERROR: Mutex for page %lu is locked!\n", i);
            // Depending on the test context, you might want to exit or handle this
            exit(1); // Exit on failure
        }
    }
     // printf("✓ All page latches verified unlocked.\n"); // Optional: success message
}

// Counts the number of valid (non-empty) entries in the kv_store.
uint64_t kv_store_count_entries(const kv_store_t* map) {
    assert(map->data != NULL);
    uint64_t num_entries = 0;
    // Calculate the number of pages based on the initialization logic
    uint64_t num_pages = (1 << (map->num_slots_log2 - KVE_NUM_SLOTS_LOG2)) + 1;

    for(uint64_t i = 0; i < num_pages - 1; i++) { // Iterate up to num_pages-1 as in init
        kvs_page_t* page = &map->data[i];
        for (int j = 0; j < KVE_NUM_SLOTS; j++) {
            // Check if the combined field indicates an empty slot
            if (page->cbo_and_ivs[j] != UINT64_MAX) {
                num_entries++;
            }
        }
    }

    // Optional: Add a more meaningful assertion if possible
    // uint64_t total_slots = (num_pages - 1) * KVE_NUM_SLOTS;
    // assert(num_entries <= total_slots);
    return num_entries;
}


int main() {
    size_t log_data_size = 0;

    printf("-- (0)\n");

    // Load the binary log file
    uint8_t* log_data_buffer = load_binary_log("assets/log-file-example-rand.bin", &log_data_size);

    if (log_data_buffer == NULL || log_data_size == 0) {
        perror("Failed to load log data. Exiting.\n");
        return 1;
    }
    printf("--\n");

    // Initialize the key-value store
    // Pass NULL as filepath and log_file buffer pointer, as kv_store_init doesn't use them directly
    kv_store_t map = kv_store_init(NULL, NULL);
    uint8_t* current_log_ptr = log_data_buffer;
    uint8_t* log_end_ptr = log_data_buffer + log_data_size;

    time_t start_time = clock();

    // Track some keys and values for GET verification
    #define MAX_TRACKED_KEYS 20 // Increased size slightly
    struct {
        char key[MAX_KEY_LEN + 1]; // Use defined max key length
        uint8_t value[MAX_VAL_LEN]; // Use defined max value length
        uint32_t value_len;
        bool exists;
    } tracked_keys[MAX_TRACKED_KEYS] = {0};
    int num_tracked_keys = 0;
    int tracked_key_idx = 0; // Index to cycle through tracked keys array

    // Test a non-existent key before any operations
    printf("\n=== Testing GET on non-existent key ===\n");
    verify_get_value(&map, "a_truly_nonexistent_key_123!@#", NULL, 0, false);

    uint64_t operation_count = 0;
    while (current_log_ptr < log_end_ptr) {
        // Ensure pointer alignment if necessary (struct kvs_command_t should be okay)
        // assert(((uintptr_t)current_log_ptr % 8) == 0); // Add if alignment issues arise

        // Calculate the length of the current command *before* processing
        uint64_t current_cmd_len = LogEntry_len(current_log_ptr);
        if (current_cmd_len == 0 || (current_log_ptr + current_cmd_len > log_end_ptr)) {
             fprintf(stderr, "Error: Invalid command length (%lu) or buffer overflow at offset %ld\n",
                current_cmd_len, (long)(current_log_ptr - log_data_buffer));
             break; // Stop processing
        }

        kvs_op_t cmd = LogEntry_get_cmd(current_log_ptr);
        char* key = LogEntry_get_key(current_log_ptr);
        uint8_t* value = NULL;
        uint32_t value_len = 0;
        LogEntry_get_value(current_log_ptr, &value, &value_len);

        // Basic validation
        assert(key != NULL);
        assert(strlen(key) <= MAX_KEY_LEN);
        if (cmd == CMD_SET) {
            assert(value_len <= MAX_VAL_LEN);
        }

        operation_count++;
        printf("\n=== Operation %lu (Offset: %ld) ===\n", operation_count, (long)(current_log_ptr - log_data_buffer));
        kv_store_assert_unlocked(&map); // Check locks before operation

        printf("Key: (%s)\n", key);
        printf("Value length: %u\n", value_len);
        printf("Value Preview: [ ");
        // Print fewer bytes for brevity
        for(uint32_t j = 0; j < value_len && j < 16; j++) {
            printf("%02X ", value[j]); // Print value bytes as hex
        }
        if (value_len > 16) printf("...");
        printf("]\n");

        // Calculate the command's byte offset from the beginning of the log buffer
        uint64_t cmd_byte_offset = (uint64_t)(current_log_ptr - log_data_buffer);

        int current_tracked_idx = -1;
        // Check if this key is already tracked
        for (int j = 0; j < MAX_TRACKED_KEYS; j++) {
             if (tracked_keys[j].key[0] != '\0' && strcmp(tracked_keys[j].key, key) == 0) {
                 current_tracked_idx = j;
                 break;
             }
        }


        if (cmd == CMD_SET) {
            printf("Command: SET\n");
            int64_t res = kv_store_set(&map, cmd_byte_offset, key, value, value_len);
            assert(res == 0); // kv_store_set returns 0 on success

            // Track or update this key for GET verification
            if (current_tracked_idx != -1) {
                 // Update existing tracked key
                 memcpy(tracked_keys[current_tracked_idx].value, value, value_len);
                 tracked_keys[current_tracked_idx].value_len = value_len;
                 tracked_keys[current_tracked_idx].exists = true;
                 printf("Updated tracked key index %d: %s\n", current_tracked_idx, key);
            } else if (num_tracked_keys < MAX_TRACKED_KEYS) {
                 // Add as a new tracked key
                 strcpy(tracked_keys[num_tracked_keys].key, key);
                 memcpy(tracked_keys[num_tracked_keys].value, value, value_len);
                 tracked_keys[num_tracked_keys].value_len = value_len;
                 tracked_keys[num_tracked_keys].exists = true;
                 printf("Added tracked key index %d: %s\n", num_tracked_keys, key);
                 num_tracked_keys++;
            } else {
                 // Overwrite the oldest tracked key using cycling index
                 current_tracked_idx = tracked_key_idx;
                 strcpy(tracked_keys[current_tracked_idx].key, key);
                 memcpy(tracked_keys[current_tracked_idx].value, value, value_len);
                 tracked_keys[current_tracked_idx].value_len = value_len;
                 tracked_keys[current_tracked_idx].exists = true;
                 printf("Overwrote tracked key index %d: %s\n", current_tracked_idx, key);
                 tracked_key_idx = (tracked_key_idx + 1) % MAX_TRACKED_KEYS;
            }


            // Immediately verify the GET operation works for this key
            verify_get_value(&map, key, value, value_len, true);

        } else if (cmd == CMD_DELETE) {
            printf("Command: DELETE\n");
            int64_t res = kv_store_delete(&map, key);
            assert(res == 0); // kv_store_delete returns 0 on success

            // Update tracked key status if it was tracked
            if (current_tracked_idx != -1) {
                 tracked_keys[current_tracked_idx].exists = false;
                 // Optionally clear value/len:
                 // tracked_keys[current_tracked_idx].value_len = 0;
                 // memset(tracked_keys[current_tracked_idx].value, 0, MAX_VAL_LEN);
                 printf("Marked tracked key index %d as deleted: %s\n", current_tracked_idx, key);
            }

            // Verify key was actually deleted
            verify_get_value(&map, key, NULL, 0, false);
        } else if (cmd == CMD_GET || cmd == CMD_NOP) {
             printf("Command: %s (Skipping modification)\n", (cmd == CMD_GET ? "GET" : "NOP"));
             // GET commands in the log usually imply a read operation happened,
             // but for state application, we typically only care about SET/DELETE.
             // NOP is explicitly ignored.
        }
        else {
            fprintf(stderr, "✗ ERROR: Unknown command type %d found in log at offset %ld\n",
                cmd, (long)(current_log_ptr - log_data_buffer));
            // Decide whether to stop or continue
            exit(1); // Exit on unknown command
        }

        uint64_t num_entries = kv_store_count_entries(&map);
        printf("Current number of entries in map: %lu\n", num_entries);

        // Advance the pointer to the next command
        current_log_ptr += current_cmd_len;

        // Optional: Add alignment jump if needed and if format guarantees it
        // current_log_ptr = jump_to_alignment(current_log_ptr, 8);

        // Periodically check all tracked keys (e.g., every 50 operations)
        if (operation_count % 50 == 0 && operation_count > 0) {
            printf("\n--- Verifying all tracked keys at operation %lu ---\n", operation_count);
             for (int j = 0; j < MAX_TRACKED_KEYS; j++) {
                 // Only verify keys that have been assigned
                 if (tracked_keys[j].key[0] != '\0') {
                     verify_get_value(&map, tracked_keys[j].key,
                                      tracked_keys[j].value,
                                      tracked_keys[j].value_len,
                                      tracked_keys[j].exists);
                 }
             }
             printf("--- End tracked key verification ---\n");
        }
    } // End while loop over log buffer

    // Check if we processed the whole buffer
    if (current_log_ptr != log_end_ptr) {
         fprintf(stderr, "Warning: Did not process the entire log buffer. Ended at offset %ld, total size %zu\n",
            (long)(current_log_ptr - log_data_buffer), log_data_size);
    }


    time_t end_time = clock();
    printf("\n=== Test completed ===\n");
    printf("Processed %lu operations from log file.\n", operation_count);
    printf("Time elapsed: %f seconds\n", ((double) end_time - (double) start_time) / (double) CLOCKS_PER_SEC);

    printf("\n=== Final verification of all tracked keys ===\n");
    for (int j = 0; j < MAX_TRACKED_KEYS; j++) {
        // Only verify keys that have been assigned
         if (tracked_keys[j].key[0] != '\0') {
             verify_get_value(&map, tracked_keys[j].key,
                              tracked_keys[j].value,
                              tracked_keys[j].value_len,
                              tracked_keys[j].exists);
        }
    }

    // Optional: Display final map contents (can be very verbose)
    // printf("\n=== Final values in kv_store ===\n");
    // kv_store_display_values(&map);

    free(log_data_buffer); // Free the buffer holding the log data
    // Note: kv_store_t itself might need a dedicated cleanup/destroy function
    // if kv_store_init allocated resources (like map.data) that need freeing.
    // Based on the provided kv_store_init, map.data IS allocated with malloc.
    if (map.data) {
         // Destroy mutexes before freeing memory
         uint64_t num_pages = (1 << (map.num_slots_log2 - KVE_NUM_SLOTS_LOG2)) + 1;
         for(uint64_t i = 0; i < num_pages -1 ; i++) {
             pthread_mutex_destroy(&map.data[i].latch);
         }
         free(map.data);
         map.data = NULL; // Prevent double free
    }


    printf("\nTest finished successfully.\n");
    return 0;
}