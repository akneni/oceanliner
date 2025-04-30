#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>
#include <stdint.h>
#include <netinet/in.h>
#include "../include/network.h"

// The maximum values for the key and the value
#define MAX_KEY_LEN UINT16_MAX
#define MAX_VAL_LEN UINT32_MAX

// Number of read-only file descriptors for the log file. 
#define LF_NUM_FDS 8

// The maximum number of nodes supported
#define MAX_NODES 256

// Size of the command buffer
#define CMDBUF_SIZE (1 << 24)

// Key-value store operation
typedef enum kvs_op_t {
    CMD_NOP,
    CMD_GET,
    CMD_SET,
    CMD_DELETE,
} kvs_op_t;

typedef struct {
    dual_format_addr_t nodes[MAX_NODES];
    int32_t node_fds[MAX_NODES];
    uint8_t self_id;
    uint8_t num_nodes;
} cluster_t;


// Key-value store Command Encoding (28 bytes without data[])
typedef struct kvs_command_t {
    uint32_t key_length;
    uint32_t value_length;
    struct sockaddr_in return_addr;
    kvs_op_t type;
    uint8_t padding[3];

    // First stores the binary data (value), then stores the key (null terminated)
    uint8_t data[];
} kvs_command_t;

typedef struct {
    uint64_t term;
    uint64_t log_index;
    size_t cmds_length;
    uint8_t* data_to_free;
    kvs_command_t data[];
} cmd_buffer_t;

typedef struct {
    int32_t file_fd;
} logfile_t;

// Types that the threads use to send messages to eachother
typedef struct {
    cmd_buffer_t* write_cmds;
    cmd_buffer_t* read_cmds;
} _1_to_3_t;

typedef struct {
    uint64_t lc_term;
    uint64_t lc_log_index;
    uint64_t node_id;
} _2_to_3_t;

#define _2_to_6_t _2_to_3_t


uint64_t fsizeof(FILE* f);
uint8_t* jump_to_alignment(uint8_t* ptr, uint64_t alignment);
void sleep_milisec(uint64_t miliseconds);

extern int32_t logfile_fd_mut;
extern int32_t logfile_fds_ro[LF_NUM_FDS];
extern cluster_t cluster_state;

#endif // GLOBALS_H
