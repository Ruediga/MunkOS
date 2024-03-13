#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// don't do this because intel doesn't have clzq or ctzq instructions
static inline uint64_t next_pow_2(uint64_t x) {
    if (!x) return 0;
    uint64_t leading = __builtin_clzl(x);
    if (sizeof(uint64_t) * 8 - (leading + __builtin_ctzl(x)) == 1) {
        return x;
    }
    return 1 << (sizeof(uint64_t) * 8 - leading);
}

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

static inline bool crosses_boundary(uint64_t start, uint64_t len, uint64_t boundary)
{
    uint64_t bound_begin = start / boundary;
    uint64_t bound_end = (start + len - 1) / boundary;
    
    return bound_begin != bound_end;
}