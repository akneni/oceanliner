#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "../include/globals.h"
#include "../include/cluster.h"
#include "../include/disk_map.h"
#include "../include/log_file.h"

// Calculate padding needed to align to 8 bytes
size_t calculate_padding(size_t offset) {
    return ((offset + 7) & ~7) - offset;
}

void load_data(char* filepath, uint8_t* buffer, size_t buffer_len) {
    FILE* file = fopen(filepath, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening file: %s\n", filepath);
        exit(1);
    }

    char line[1024]; // Assuming maximum line length is 1024 characters
    uint8_t* current_buffer = buffer;
    size_t remaining_buffer = buffer_len;

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
        StateCommand command;
        if (strcmp(command_str, "SET") == 0) {
            command = SC_SET;
            // For SET command, value_str must be present
            if (value_str == NULL) {
                fprintf(stderr, "Error: SET command requires a value\n");
                fclose(file);
                exit(1);
            }
        } else if (strcmp(command_str, "DELETE") == 0) {
            command = SC_DELETE;
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
        uint8_t value[1024]; // Assuming maximum number of values is 1024
        size_t value_len = 0;
        
        if (value_str != NULL && command == SC_SET) {
            // Make a copy of the value string for parsing
            char value_str_copy[1024];
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

        // Update remaining buffer
        remaining_buffer -= entry_size;
    }

    fclose(file);
}

int main() {
    uint64_t buffer_len = 10000000;
    uint8_t* log_file = malloc(buffer_len);
    load_data("assets/log-file-example-1.txt", log_file, buffer_len);

    DiskMap map = DiskMap_init('--', log_file);

    for(int i = 0; i < 2; i++) {
        char* s = LogEntry_get_key(log_file);
        assert(s != NULL);
    
        printf("(%s)\n", s);
    
        uint8_t* value;
        uint64_t value_len;
        LogEntry_get_value(log_file, &value, &value_len);
        
        assert(value_len < 1024);

        printf("[ ");
        for(int i = 0; i < value_len; i++) {
            printf("%hu ", value[i]);
        }
        printf("]\n\n\n");
    
        log_file += LogEntry_len(log_file);        
    }






    return 0;
}