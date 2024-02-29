#pragma once

#include <stdint.h>
#include <stddef.h>

static inline uint64_t next_pow_2(uint64_t x) {
    if (!x) return 0;
    uint64_t leading = __builtin_clzl(x);
    if (sizeof(uint64_t) * 8 - (leading + __builtin_ctzl(x)) == 1) {
        return x;
    }
    return 1 << (sizeof(uint64_t) * 8 - leading);
}