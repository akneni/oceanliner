#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>

#define ADDR_IP_LEN 20
#define ADDR_PORT_LEN 6

typedef struct Addr {
    char ip[ADDR_IP_LEN];
    char port[ADDR_PORT_LEN];
} Addr;


uint64_t fsizeof(FILE* f);

#endif // UTILS_H