#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdalign.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include "../include/client_ops.h"

void resp_to_client(int32_t fd, struct sockaddr_in* addr, client_resp_t* msg) {
    assert(msg->status_code == 200 || msg->status_code == 500);

    size_t msg_len = sizeof(client_resp_t) + msg->body_length;
    
    sendto(fd, msg, msg_len, 0, (struct sockaddr*) addr, sizeof(struct sockaddr));
}