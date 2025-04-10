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
uint8_t* jump_to_alignment(uint8_t* ptr, uint64_t alignment) {
    uint64_t addr = (uint64_t)ptr;
    uint64_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    return (uint8_t*)aligned_addr;
}