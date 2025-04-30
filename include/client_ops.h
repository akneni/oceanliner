#ifndef CLIENT_OPS_H
#define CLIENT_OPS_H

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

typedef struct {
    size_t body_length;
    uint16_t status_code; // 200 for OK, 500 for server failure
    uint8_t padding[6];
    uint8_t data[];
} client_resp_t;

void resp_to_client(int32_t fd, struct sockaddr_in* addr, client_resp_t* msg);

#endif