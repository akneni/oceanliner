#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../include/utils.h"


struct sockaddr_in server_addr;

struct sockaddr*)&server_addr, sizeof(server_addr));


int32_t hl_connect(Addr* addr);

#endif // NETWORK_H