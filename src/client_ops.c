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
#include <sys/socket.h>

#include <liburing.h>

#include "../include/client_ops.h"

void resp_to_client(int32_t fd, struct sockaddr_in* addr, client_resp_t* msg) {
    assert(msg->status_code >= 200 && msg->status_code <= 599);
    assert(fd > 0);
    assert(msg != NULL);
    assert(msg->op_type == CMD_GET || msg->op_type == CMD_SET || msg->op_type == CMD_DELETE);

    size_t msg_len = sizeof(client_resp_t) + msg->body_length;


    int32_t res = sendto(fd, msg, msg_len, 0, (struct sockaddr*) addr, sizeof(struct sockaddr));

    if (res <= 0) {
        perror("failed to send message to client");
        exit(1);
    }
}

void resp_to_client_uring(int32_t fd, struct sockaddr_in* addr, client_resp_t* msg, struct io_uring_sqe* sqe) {
    assert(msg->status_code >= 200 && msg->status_code <= 599);
    assert(fd > 0);
    assert(msg != NULL);
    assert(msg->op_type == CMD_GET || msg->op_type == CMD_SET || msg->op_type == CMD_DELETE);

    size_t msg_len = sizeof(client_resp_t) + msg->body_length;

    struct msghdr ms_addr;
    struct iovec iov;

    memset(&ms_addr, 0, sizeof(ms_addr));

    iov.iov_base = msg;
    iov.iov_len = msg_len;

    ms_addr.msg_name = addr;
    ms_addr.msg_namelen = sizeof(struct sockaddr_in);


    ms_addr.msg_iov = &iov;
    ms_addr.msg_iovlen = 1;

    ms_addr.msg_control = NULL;
    ms_addr.msg_controllen = 0;

    io_uring_prep_sendmsg(sqe, fd, &ms_addr, 0);
}