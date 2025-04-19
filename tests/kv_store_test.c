TODO: fix this

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <time.h>

#include "../include/globals.h"
#include "../include/cluster.h"
#include "../include/disk_map.h"
#include "../include/log_file.h"

// Function to align pointers to specified alignment
uint8_t* jump_to_alignment(uint8_t* ptr, size_t alignment) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return (uint8_t*)aligned;
}

// Calculate padding needed to align to 8 bytes
size_t calculate_padding(size_t offset) {
    return ((offset + 7) & ~7) - offset;
}

// Helper function to extract command from log entry
kvs_op_t LogEntry_get_cmd(uint8_t* log_entry) {
    kvs_command_t* cmd = (kvs_command_t*)log_entry;
    return cmd->type;
}

// Helper function to extract key from log entry
char* LogEntry_get_key(uint8_t* log_entry) {
    kvs_command_t* cmd = (kvs_command_t*)log_entry;
    return kvs_command_get_key(cmd);
}

// Helper function to extract value from log entry
void LogEntry_get_value(uint8_t* log_entry, uint8_t** value, uint64_t* value_len) {
    kvs_command_t* cmd = (kvs_command_t*)log_entry;
    kvs_command_get_value(cmd, value);
    *value_len = cmd->value_length;
}

// Helper function to get log entry length
uint64_t LogEntry_len(uint8_t* log_entry) {
    kvs_command_t* cmd = (kvs_command_t*)log_entry;
    return kvs_command_len(cmd);
}

// Helper function to verify GET results match expected values
void verify_get_value(kv_store_t* map, const char* key, uint8_t* expected_value, 
                      size_t expected_len, bool should_exist) {
    int64_t actual_len = 0;
    uint8_t* actual_value = DiskMap_get(map, key, &actual_len);
    
    if (!should_exist) {
        if (actual_len == -1) {
            printf("✓ Key '%s' correctly not found\n", key);
        } else {
            printf("✗ ERROR: Key '%s' was found but shouldn't exist\n", key);
            free(actual_value);
            exit(1);
        }
        return;
    }
    
    if (actual_len == -1) {
        printf("✗ ERROR: Key '%s' not found but should exist\n", key);
        exit(1);
    }
    
    if (actual_len != expected_len) {
        printf("✗ ERROR: Value length mismatch for key '%s': expected %lu, got %ld\n", 
               key, expected_len, actual_len);
        free(actual_value);
        exit(1);
    }
    
    for (size_t i = 0; i < expected_len; i++) {
        if (actual_value[i] != expected_value[i]) {
            printf("✗ ERROR: Value mismatch for key '%s' at index %lu: expected %d, got %d\n", 
                   key, i, expected_value[i], actual_value[i]);
            free(actual_value);
            exit(1);
        }
    }
    
    printf("✓ Key '%s' correctly retrieved with matching value\n", key);
    free(actual_value);
}

uint64_t load_data(char* filepath, uint8_t* buffer, size_t buffer_len) {
    FILE* file = fopen(filepath, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening file: %s\n", filepath);
        exit(1);
    }

    size_t max_line_length = 1 << 16;

    char line[max_line_length]; // Assuming maximum line length is 1024 characters
    uint8_t* current_buffer = buffer;
    size_t remaining_buffer = buffer_len;
    uint64_t num_commands = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        // Remove newline character if present
        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
            line_len--;
        }

        // Skip empty lines
        if (line_len == 0) {
            continue;
        }

        // Parse the line manually to avoid issues with strtok
        char* command_str = line;
        char* key = NULL;
        char* value_str = NULL;
        
        // Find the first pipe
        char* first_pipe = strchr(line, '|');
        if (first_pipe == NULL) {
            fprintf(stderr, "Error parsing line (no pipe found): %s\n", line);
            fclose(file);
            exit(1);
        }
        
        // Null-terminate the command string and set key to the next character
        *first_pipe = '\0';
        key = first_pipe + 1;
        
        // Find the second pipe
        char* second_pipe = strchr(key, '|');
        if (second_pipe != NULL) {
            // Null-terminate the key string and set value_str to the next character
            *second_pipe = '\0';
            value_str = second_pipe + 1;
        }

        // Determine command type
        kvs_op_t command;
        if (strcmp(command_str, "SET") == 0) {
            command = CMD_SET;
            // For SET command, value_str must be present
            if (value_str == NULL) {
                fprintf(stderr, "Error: SET command requires a value\n");
                fclose(file);
                exit(1);
            }
        } else if (strcmp(command_str, "DELETE") == 0) {
            command = CMD_DELETE;
            // For DELETE command, there's no value
            value_str = NULL;
        } else {
            fprintf(stderr, "Unknown command: %s\n", command_str);
            fclose(file);
            exit(1);
        }

        // Calculate key length (including null terminator)
        size_t key_len = strlen(key) + 1;
        
        // Calculate the offset after storing command and key
        size_t offset_after_key = 1 + key_len;  // 1 byte for command + key length (including null terminator)
        
        // Calculate padding needed to align value_length to 8-byte boundary
        size_t padding = calculate_padding(offset_after_key);
        
        // Parse values if present
        uint8_t value[max_line_length]; // Assuming maximum number of values is 1024
        size_t value_len = 0;
        
        if (value_str != NULL && command == CMD_SET) {
            // Make a copy of the value string for parsing
            char value_str_copy[max_line_length];
            strcpy(value_str_copy, value_str);
            
            // Parse the comma-separated values
            char* token = strtok(value_str_copy, ",");
            while (token != NULL && value_len < 1024) {
                // Convert the string to an integer
                char* endptr;
                long val = strtol(token, &endptr, 10);
                
                // Check for conversion errors
                if (*endptr != '\0') {
                    fprintf(stderr, "Error parsing value: %s\n", token);
                    fclose(file);
                    exit(1);
                }
                
                // Ensure the value fits in an uint8_t
                if (val < 0 || val > 255) {
                    fprintf(stderr, "Value out of range for uint8_t: %ld\n", val);
                    fclose(file);
                    exit(1);
                }
                
                value[value_len++] = (uint8_t)val;
                token = strtok(NULL, ",");
            }
        }

        // Calculate total size needed for this entry
        size_t entry_size = 1 + key_len + padding + 8 + value_len;  // command + key + padding + value_length + value
        
        // Check if buffer has enough space
        if (entry_size > remaining_buffer) {
            fprintf(stderr, "Buffer is not large enough\n");
            fclose(file);
            exit(1);
        }

        // Write command
        *current_buffer = command;
        current_buffer += 1;

        // Write key (including null terminator)
        memcpy(current_buffer, key, key_len);
        current_buffer += key_len;

        // Add padding
        memset(current_buffer, 0, padding);
        current_buffer += padding;

        // Write value_length (ensuring 8-byte alignment)
        uint64_t val_len = value_len;
        memcpy(current_buffer, &val_len, 8);
        current_buffer += 8;

        // Write value if present
        if (value_len > 0) {
            memcpy(current_buffer, value, value_len);
            current_buffer += value_len;
        }

        current_buffer = jump_to_alignment(current_buffer, 8);
        num_commands += 1;

        // Update remaining buffer
        remaining_buffer -= (entry_size + 7);
    }

    fclose(file);
    return num_commands;
}

void DiskMap_assert_unlocked(const kv_store_t* map) {
    for(int i = 0; i < (1 << map->num_slots_log2); i++) {
        kvs_page_t* page = &map->data[i];

        int res = pthread_mutex_trylock(&page->latch);
        assert(res == 0);
        pthread_mutex_unlock(&page->latch);
    }
}

uint64_t DiskMap_count_entries(const kv_store_t* map) {
    assert(map->data != NULL);
    uint64_t num_entries = 0;

    for(int i = 0; i < (1 << map->num_slots_log2); i++) {
        kvs_page_t* page = &map->data[i];
        for (int j = 0; j < KVE_NUM_SLOTS; j++) {
            // Fixed: Access the element in the array rather than comparing the array pointer
            if (page->cbo_and_ivs[j] != UINT64_MAX) {
                num_entries++;
            }
        }
    }

    assert(num_entries <= (1 << map->num_slots_log2));
    return num_entries;
}

int main() {
    uint64_t buffer_len = 10000000;
    uint8_t* log_file = malloc(buffer_len);
    uint64_t num_commands = load_data("assets/log-file-example-rand.txt", log_file, buffer_len);

    // Fixed: Use string literals with double quotes instead of character literal
    kv_store_t map = DiskMap_init("--", log_file);
    uint64_t log_file_start = (uint64_t) log_file;

    time_t start_time = clock();
    
    // Track some keys and values for GET verification
    #define MAX_TRACKED_KEYS 10
    struct {
        char key[256];
        uint8_t value[1024];
        uint64_t value_len;
        bool exists;
    } tracked_keys[MAX_TRACKED_KEYS] = {0};
    int num_tracked_keys = 0;

    // Test a non-existent key before any operations
    printf("\n=== Testing GET on non-existent key ===\n");
    verify_get_value(&map, "nonexistent_key", NULL, 0, false);
    
    for(int i = 0; i < num_commands; i++) {
        kvs_op_t cmd = LogEntry_get_cmd(log_file);

        char* s = LogEntry_get_key(log_file);
        assert(s != NULL);
    
        printf("\n=== Operation %d ===\n", i+1);
        DiskMap_assert_unlocked(&map);

        printf("Key: (%s)\n", s);
    
        uint8_t* value;
        uint64_t value_len = 0;

        LogEntry_get_value(log_file, &value, &value_len);
        
        printf("Value length: %lu\n", value_len);
        assert(value_len < 1024);

        printf("Value: [ ");
        for(int j = 0; j < (value_len & 0xF); j++) {
            printf("%hu ", value[j]);
        }
        printf("]\n");
    
        if (cmd == CMD_SET) {
            printf("Command: SET\n");
            uint64_t cmd_offset = ((uint64_t) log_file) - log_file_start;
            
            // Fixed: Pass the key_hash instead of the raw key
            key_hash_t key_hash = hash(s);
            int64_t res = DiskMap_set(&map, cmd_offset, s, value, value_len);
            assert(res >= 0);
            
            // Track this key for GET verification
            if (num_tracked_keys < MAX_TRACKED_KEYS) {
                strcpy(tracked_keys[num_tracked_keys].key, s);
                memcpy(tracked_keys[num_tracked_keys].value, value, value_len);
                tracked_keys[num_tracked_keys].value_len = value_len;
                tracked_keys[num_tracked_keys].exists = true;
                num_tracked_keys++;
            }
            
            // Immediately verify the GET operation works for this key
            verify_get_value(&map, s, value, value_len, true);
        }
        else if (cmd == CMD_DELETE) {
            printf("Command: DELETE\n");
            int64_t res = DiskMap_delete(&map, s);
            
            // Update tracked key status
            for (int j = 0; j < num_tracked_keys; j++) {
                if (strcmp(tracked_keys[j].key, s) == 0) {
                    tracked_keys[j].exists = false;
                    break;
                }
            }
            
            // Verify key was actually deleted
            verify_get_value(&map, s, NULL, 0, false);
        }
        else {
            printf("Bad command: %d\n", cmd);
            printf("num_commands = %lu\n", num_commands);
            exit(1);
        }

        uint64_t num_entries = DiskMap_count_entries(&map);
        printf("Number of entries: %lu\n", num_entries);

        log_file += LogEntry_len(log_file);
        log_file = jump_to_alignment(log_file, 8);
        
        // Periodically check all tracked keys
        if (i % 5 == 0 && i > 0) {
            printf("\n=== Verifying all tracked keys at operation %d ===\n", i);
            for (int j = 0; j < num_tracked_keys; j++) {
                verify_get_value(&map, tracked_keys[j].key, 
                                tracked_keys[j].value, 
                                tracked_keys[j].value_len, 
                                tracked_keys[j].exists);
            }
        }
    }

    time_t end_time = clock();
    printf("\n=== Test completed ===\n");
    printf("Time elapsed: %f seconds\n", ((double) end_time - (double) start_time) / (double) CLOCKS_PER_SEC);

    printf("\n=== Final verification of all tracked keys ===\n");
    for (int j = 0; j < num_tracked_keys; j++) {
        verify_get_value(&map, tracked_keys[j].key, 
                        tracked_keys[j].value, 
                        tracked_keys[j].value_len, 
                        tracked_keys[j].exists);
    }

    printf("\n=== All values in disk map ===\n");
    DiskMap_display_values(&map);
    free(log_file);

    return 0;
}