#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "../include/log_file.h"
#include "../include/globals.h"
#include "../include/xxhash.h"

size_t kvs_command_len(const kvs_command_t* log_entry) {
    assert(((uint64_t) log_entry) % 8 == 0);

    size_t length = (
        sizeof(kvs_command_t) +
        (size_t) log_entry->key_length +
        1 + // null terminator 
        (size_t) log_entry->value_length
    );

    return jump_to_alignment(length, 8);
}

char* kvs_command_get_key(const kvs_command_t* log_entry) {
    assert(((uint64_t) log_entry) % 8 == 0);
    uint8_t const* key_ptr = &log_entry->data[log_entry->value_length];
    key_ptr = (uint8_t*) jump_to_alignment((size_t) key_ptr, 4);
    return (char*) key_ptr;
}

void kvs_command_get_value(kvs_command_t* log_entry, uint8_t** value) {
    assert(((uint64_t) log_entry) % 8 == 0);
    *value = log_entry->data;
}

void kvsb_header_calc_checksum(kvsb_header_t* header) {
    header->header_checsum = MAGIC_NUMBER;
    XXH128_hash_t checksum = XXH3_128bits(header, sizeof(header));
    header->header_checsum = checksum;
}

bool kvsb_header_validate_checksum(kvsb_header_t* header) {
    XXH128_hash_t checksum = header->header_checsum;

    header->header_checsum = MAGIC_NUMBER;
    XXH128_hash_t checksum_found = XXH3_128bits(header, sizeof(header));
    header->header_checsum = checksum;

    return XXH128_cmp(&checksum, &checksum_found) == 0;
}

void logfile_get_data(logfile_t* logfile, uint64_t cmd_offset, uint8_t* out_buffer, size_t length) {
    char* fname = "assets/log-file-example-rand.bin";

    FILE* fp = fopen(fname, "r");

    fseek(fp, cmd_offset, 0);

    fread(out_buffer, 1, length, fp);
    
    fclose(fp);
}

void logfile_append_data(logfile_t* logfile, cmd_buffer_t* cmd_buffer) {
    kvsb_header_t header = {
        .term = cmd_buffer->term,
        .log_index = cmd_buffer->log_index,
        .data_length = cmd_buffer->cmds_length,
        .num_commands = 0,
    };

    kvsb_header_calc_checksum(&header);

    lseek(logfile->file_fd, 0, SEEK_END);
    write(logfile->file_fd, &header, sizeof(header));
    write(logfile->file_fd, cmd_buffer->data, header.data_length);
    write(logfile->file_fd, &header, sizeof(header));
}

/// @brief get last term and index
/// @param logfile 
/// @param term 
/// @param index 
/// @return 
void logfile_get_last_tai(logfile_t* logfile, uint64_t* term, uint64_t* index) {
    size_t length = logfile_sizeof(logfile);
    if (length < sizeof(logfile_t)) {
        assert(length == 0);
        *term = 0;
        *index = 0;
        return;
    }

    lseek(logfile->file_fd, -1 * sizeof(kvsb_header_t), SEEK_END);

    kvsb_header_t header;
    read(logfile->file_fd, &header, sizeof(kvsb_header_t));
    assert(kvsb_header_validate_checksum(&header) && "kvsb_header_t checksum validation failed");

    *term = header.term;
    *index = header.log_index;
}

size_t logfile_sizeof(logfile_t* logfile) {
    struct stat file_stat;

    if (fstat(logfile->file_fd, &file_stat) < 0) {
        perror("fstat failed");
        close(logfile->file_fd);
        exit(EXIT_FAILURE);
    }

    return (size_t) file_stat.st_size;
}