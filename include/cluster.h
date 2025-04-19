#ifndef CLUSTER_H
#define CLUSTER_H

#include <stdint.h>
#include "globals.h"


typedef struct Cluster {
    uint64_t length;
    int32_t fds[256];
    dual_format_addr_t addrs[256];
} Cluster;



#endif // CLUSTER_H