#pragma once

#include <stddef.h>
#include <stdint.h>

#include "frame_alloc.h"

// only cache kmalloc allocs of this size
#define KMALLOC_MAX_CACHE_SIZE (PAGE_SIZE * 2)
// 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 -> 10
// assuming pow2
#define KMALLOC_ALLOC_MIN 16
#define KMALLOC_ALLOC_MAX 8192
#define KMALLOC_ALLOC_SIZES 10

// enables allocate use-after-free sanitizer and overflow checks
#define CONFIG_SLAB_SANITIZE

#ifdef CONFIG_SLAB_SANITIZE
// maybe just zero it?
#define KMALLOC_SANITIZE_BYTE ((uint8_t)0b00010001)
#define KMALLOC_DOUBLE_FREE_QWORD ((uint64_t)0x1010101010101010)
// when compiling with redzone paddings, the alignment guarantees change
#define KMALLOC_REDZONE_LEFT 64
#define KMALLOC_REDZONE_RIGHT 64
#endif

extern size_t slab_initialized;

void slab_init();
void *kmalloc(size_t size);
void kfree(void *addr);
void *krealloc(void *addr, size_t size);
void *kcalloc(size_t entries, size_t size);
void slab_dbg_print(void);