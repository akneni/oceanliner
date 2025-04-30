#define _GNU_SOURCE


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>



#include "../include/globals.h"
#include "../include/network.h"


void init_client_listeners(client_handeling_t* clients, uint8_t self_id) {
    int32_t res;

    clients->client_socket_recv = socket(AF_INET, SOCK_DGRAM, 0);

    // Set timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = CLIENT_READ_TIMEOUT * 1000;
    res = setsockopt(clients->client_socket_recv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint16_t port = 9000 + self_id;
    struct sockaddr_in addr;
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", (void*) &addr.sin_addr);

    res = bind(clients->client_socket_recv, (struct sockaddr*) &addr, sizeof(addr));
    if (res < 0) {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }
    
    clients->clinet_socket_send = socket(AF_INET, SOCK_DGRAM, 0);
    addr.sin_port = htons(10000 + self_id);

    res = bind(clients->clinet_socket_send, (struct sockaddr*) &addr, sizeof(addr));
    if (res < 0) {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }

}