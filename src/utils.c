#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include "../include/utils.h"


uint64_t fsizeof(FILE* f) {
    int64_t cur_pos = ftell(f);

    fseek(f, 0, SEEK_END);
    int64_t length = ftell(f);
    fseek(f, 0, cur_pos);
    return length;
}

