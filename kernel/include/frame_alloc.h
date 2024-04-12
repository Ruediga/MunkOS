#pragma once

#include <limine.h>
#include <stdint.h>
#include <stddef.h>

#include "cpu.h"

#define PAGE_SIZE (0x1000ul)
#define PAGE_SHIFT (12ul)

#define BUDDY_HIGH_ORDER (10ul)
#define BUDDY_LOW_ORDER (0ul)

// config allocator (don't move this)
// xxx_BUDDY and xxx_BITMAP are configurable
#define MUNKOS_CONFIG_BUDDY

// do not move this
#if defined(MUNKOS_CONFIG_BITMAP) && defined(MUNKOS_CONFIG_BUDDY)\
    || !defined(MUNKOS_CONFIG_BITMAP) && !defined(MUNKOS_CONFIG_BUDDY)
#error "only one allocator config macro may be defined"
#endif

struct page;

extern struct page *pages;
// copy of limines memmap
extern struct memmap_entry *memmap;
extern size_t memmap_entry_count;
extern struct limine_hhdm_response *hhdm;

// only used once in the vmm init function
struct early_mem_alloc_mapping {
    uintptr_t start;
    size_t length;
};

extern struct early_mem_alloc_mapping early_mem_mappings[];
extern size_t early_mem_allocations;

// 20 bytes
struct page {
    int i;
    union {
        struct { // allocator free list
            struct page *next;
            struct page *prev;
        };
    };
};

// fill this structure up with phys_stat_memory(struct phys_mem_stat *stat),
// so atomicity is guaranteed
struct phys_mem_stat {
    size_t total_pages,
           usable_pages,
           free_pages;
};

struct memmap_entry {
    uintptr_t start;
    size_t length;
    size_t type;
};

// frame_alloc.c
void allocator_init();
// call this for physically contiguous, pow2 sized blocks
struct page *page_alloc(size_t order);
void *page_alloc_temp(size_t order);                // remove
void page_free(struct page *page, size_t order);
void page_free_temp(void *address, size_t size);    // remove
void phys_stat_memory(struct phys_mem_stat *stat);

// mem_init.c
size_t early_mem_init();
size_t early_mem_exit(void);
void *early_mem_alloc(size_t size);
void early_mem_statistics(size_t *usable, size_t *free);
void early_mem_dbg_print(void);

static inline struct page *phys2page(uintptr_t phys) {
    return &pages[phys / PAGE_SIZE];
}

// struct page * to idx (= offset into mempages table)
static inline size_t page2idx(struct page *page) {
    return ((uintptr_t)page - (uintptr_t)pages) / sizeof(struct page);
}

// returns in what category a range falls, in case of unalignment, returns the higher order
static inline size_t size2order(size_t size) {
    size_t order = 0;
    while (size > PAGE_SIZE) {
        size >>= 1;
        order++;
    }
    return order;
}

static inline size_t order2size(size_t order) {
    return 1 << order;
}