#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include "../include/globals.h"


uint64_t fsizeof(FILE* f) {
    int64_t cur_pos = ftell(f);

    fseek(f, 0, SEEK_END);
    int64_t length = ftell(f);
    fseek(f, 0, cur_pos);
    return length;
}

/// @brief Jumps to the next memory address that's `alignment` byte aligned
/// @param ptr Pointer to align
/// @param alignment Alignment boundary (e.g., 8 for 8-byte alignment)
/// @return Aligned pointer
size_t jump_to_alignment(size_t ptr, size_t alignment) {
    size_t addr = ptr;
    size_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    return aligned_addr;
}

/// @brief Sleeps for the specified number of miliseconds
/// @param miliseconds 
void sleep_milisec(uint64_t miliseconds) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = miliseconds * 1000000L;
    nanosleep(&ts, NULL);
}