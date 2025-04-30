#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "globals.h"

// Presentation address length
#define P_ADDR_LEN 22

// The number of sockets we use to listen for client commands. 
#define NUM_CLIENT_LISTENERS 1

// The timeout in miliseconds that the thread has to read from the UDP client socket
#define CLIENT_READ_TIMEOUT 10

typedef struct {
    char presentation_ip[P_ADDR_LEN];
    uint16_t presentation_port;
    struct sockaddr_in addr;
} dual_format_addr_t;

typedef struct {
    int32_t client_socket_recv;
    int32_t clinet_socket_send;
} client_handeling_t;

int32_t hl_connect(dual_format_addr_t* addr);
void init_client_listeners(client_handeling_t* clients, uint8_t self_id);

#endif // NETWORK_H