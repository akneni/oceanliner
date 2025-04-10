#ifndef CLUSTER_H
#define CLUSTER_H

#include <stdint.h>


typedef struct Cluster {
    uint64_t length;
    int32_t fds[256];
    Addr addrs[256];
} Cluster;



#endif // CLUSTER_H