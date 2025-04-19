#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>
#include <stdint.h>
#include <netinet/in.h>


#define P_ADDR_LEN 22

// The maximum values for the key and the value
#define MAX_KEY_LEN UINT16_MAX
#define MAX_VAL_LEN UINT32_MAX

// Magic number we replace the headers of the hash field with before hashing
#define MAGIC_NUMBER (hash_128bi){.hash_p1 = 15269755912704193040ULL, .hash_p2 = 9723239452457707023ULL}


typedef struct dual_format_addr_t {
    char presentation_ip[P_ADDR_LEN];
    struct sockaddr_in addr;
    uint16_t port;
} dual_format_addr_t;

// 128 bit hash
typedef struct hash_128bi {
    uint64_t hash_p1;
    uint64_t hash_p2;
} hash_128bi;



uint64_t fsizeof(FILE* f);
uint8_t* jump_to_alignment(uint8_t* ptr, uint64_t alignment);

#endif // GLOBALS_H
