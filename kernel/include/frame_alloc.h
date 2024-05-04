#pragma once

#include <limine.h>
#include <stdint.h>
#include <stddef.h>

#include "cpu.h"
#include "interrupt.h"

#define STRUCT_PAGE_ALIGNMENT (16)

#define PAGE_SIZE (0x1000ul)
#define PAGE_SHIFT (12ul)

#define BUDDY_HIGH_ORDER (10ul)
#define BUDDY_LOW_ORDER (0ul)

// struct page flag bits (32 bit signed int)
#define STRUCT_PAGE_FLAG_COMPOSITE_TAIL (1 << 1)
#define STRUCT_PAGE_FLAG_SLAB_COMPOSITE_HEAD (1 << 2)
#define STRUCT_PAGE_FLAG_KMALLOC_BUDDY (1 << 3)

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
extern size_t pages_count;
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

// current size: 40 bytes
struct page {
    int32_t flags;  // make sure to have these set correctly
    union {
        struct {    // buddy allocator free list
            struct page *next;
            struct page *prev;
        };
        struct {    // for slab, theres a seperately defined structure (sync this up!)
            void *slab_next;            // struct page *
            void *slab_prev;            // struct page *
            void *this_cache;           // struct slab_cache *this_cache;
            void *freelist;             // pointer to first object
            uint16_t used_objs;
            uint16_t total_objs;
        };
        struct {    // slab buddy page
            size_t order;
        };
        struct {    // composite page:
                    // if page is part of a composite page,
                    // we can use this to save a reference to
                    // the head of the composite page
            struct page *comp_head;
        };
    };
} comp_aligned(STRUCT_PAGE_ALIGNMENT);

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
    if (page < pages || page >= (pages + pages_count)) {
        kpanic(0, NULL, "page %p is not in range\n", page);
    }
    return ((uintptr_t)page - (uintptr_t)pages) / sizeof(struct page);
}

static inline uintptr_t page2phys(struct page *page) {
    return page2idx(page) << PAGE_SHIFT;
}

// returns in what page size category a range falls, in case of unalignment, returns the higher order
static inline size_t psize2order(size_t size) {
    size_t order = 0;
    while (size > PAGE_SIZE) {
        size >>= 1;
        order++;
    }
    return order;
}

// returns pow2 order, in case of unalignment, returns the higher order
static inline size_t size2order(size_t size) {
    size_t order = 0;
    while (size > 1) {
        size >>= 1;
        order++;
    }
    return order;
}

static inline size_t order2size(size_t order) {
    return 1 << order;
}