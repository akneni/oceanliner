#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>


#define VALUE_MAX_SIZE 1024

#define ADDR_IP_LEN 20
#define ADDR_PORT_LEN 6


typedef enum StateCommand {
    SC_NOP,
    SC_GET,
    SC_SET,
    SC_DELETE,
} StateCommand;

typedef struct Addr {
    char ip[ADDR_IP_LEN];
    char port[ADDR_PORT_LEN];
} Addr;


uint64_t fsizeof(FILE* f);
uint8_t* jump_to_alignment(uint8_t* ptr, uint64_t alignment);

#endif // GLOBALS_H