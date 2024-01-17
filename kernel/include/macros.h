#pragma once

// macros
// ======

// max / min
#define MAX(x, y) (x > y ? x : y)
#define MIN(x, y) (x < y ? x : y)

// divisions
#define DIV_ROUNDUP(x, y) (((x) + (y) - 1) / (y))
#define DIV_ROUNDDOWN(x, y) (x / y)

// Bitmap functions
#define BITMAP_SET_BIT(bitmap, bit_index) ((bitmap)[(bit_index) / 8] |= (1 << ((bit_index) % 8)))
#define BITMAP_UNSET_BIT(bitmap, bit_index) ((bitmap)[(bit_index) / 8] &= ~(1 << ((bit_index) % 8)))
#define BITMAP_READ_BIT(bitmap, bit_index) ((bitmap)[(bit_index) / 8] & (1 << ((bit_index) % 8)))

// alignment
#define ALIGN_UP(x, base) ((x + base - 1) & ~(base - 1))
#define ALIGN_DOWN(x, base) (x & ~(base - 1))

// extract bits
// EXTRACT_BITS(ul, ul, ul)
#define EXTRACT_BITS(value, start_index, end_index) (((value) & (((1ul << ((end_index) - (start_index) + 1)) - 1) << (start_index))) >> (start_index))

// typedefs
// ========

// linker symbol
typedef char linker_symbol_ptr[];