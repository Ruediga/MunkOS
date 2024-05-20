#pragma once

#include "compiler.h"

// macros
// ======

#define kassert(cond) do { \
        if (!(cond)) { \
            kpanic(0, NULL, "assertion of %s failed at %s:%d in %s\n", #cond, __FILE__, __LINE__, __FUNCTION__); \
            unreachable(); \
        } \
    } while (0);

#define NNULL(x) ((x) ? (x) : (kpanic(0, NULL, "NULL check at failed at %s:%d\n", __FILE__, __LINE__), (void *)0))

// Bitmap functions
#define BITMAP_SET_BIT(bitmap, bit_index) ((bitmap)[(bit_index) / 8] |= (1 << ((bit_index) % 8)))
#define BITMAP_UNSET_BIT(bitmap, bit_index) ((bitmap)[(bit_index) / 8] &= ~(1 << ((bit_index) % 8)))
#define BITMAP_READ_BIT(bitmap, bit_index) ((bitmap)[(bit_index) / 8] & (1 << ((bit_index) % 8)))

// alignment
#define ALIGN_UP(x, base) (((x) + (base) - 1) & ~((base) - 1))
#define ALIGN_DOWN(x, base) ((x) & ~((base) - 1))

// convert bytes to other sizes
#define KiB (0x400)
#define MiB (KiB * 0x400)
#define GiB (MiB * 0x400)

// extract bits
// EXTRACT_BITS(ul, ul, ul)
#define EXTRACT_BITS(value, start_index, end_index) (((value) & (((1ul << ((end_index) - (start_index) + 1)) - 1) << (start_index))) >> (start_index))

// math
#define POW(base, exponent) ({ \
    uint64_t out = 1; \
    for (uint64_t i = 0; i < exponent; i++) { \
        out *= base; \
    } \
    out; \
})

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define DIFF(x, y) (MAX((x), (y)) - MIN((x), (y)))

// divisions
#define DIV_ROUNDUP(x, y) (((x) + (y) - 1) / (y))
#define DIV_ROUNDDOWN(x, y) ((x) / (y))

// typedefs
// ========

// linker symbol
typedef char linker_symbol_ptr[];