#pragma once

#include <limine.h>
#include <stdint.h>
#include <stddef.h>

// 4096 byte pages
#define PAGE_SIZE 0x1000ul

// keep track of where on the bitmap are usable areas,
// so time is saved when looking for free pages
struct __attribute__((packed)) pmm_usable_entry {
    uintptr_t base;
    size_t length;
};
extern struct pmm_usable_entry *pmm_usable_entries; 

extern struct limine_memmap_response *memmap;
extern struct limine_hhdm_response *hhdm;

// keep track of free and used pages: 0 = unused
extern uint8_t *pmm_page_bitmap;
extern uint64_t pmm_pages_usable;
extern uint64_t pmm_pages_reserved;
extern uint64_t pmm_pages_total;
extern uint64_t pmm_pages_in_use;

extern uint64_t pmm_highest_address_memmap;
extern uint64_t pmm_memmap_usable_entry_count;
extern uint64_t pmm_total_bytes_pmm_structures;
extern uint64_t pmm_bitmap_size_bytes;
extern uint64_t pmm_highest_address_usable;

void initPMM(void);
void *pmmClaimContiguousPages(size_t count);
void pmmFreeContiguousPages(void *ptr, size_t count);