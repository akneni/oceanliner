#ifndef CLIENT_OPS_H
#define CLIENT_OPS_H

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "../include/log_file.h"
#include "../include/globals.h"

// 16 bytes
typedef struct {
    size_t body_length;
    kvs_op_t op_type;
    uint16_t status_code; // 200 for OK, 500 for server failure    
    uint16_t padding;

    uint8_t data[];
} client_resp_t;

void resp_to_client(int32_t fd, struct sockaddr_in* addr, client_resp_t* msg);

#endif // CLIENT_OPS_H