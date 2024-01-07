#pragma once

#include <limine.h>
#include <stdint.h>
#include <stddef.h>

// 4096 byte pages
#define PAGE_SIZE 0x1000

extern struct limine_memmap_response *memmap;
extern struct limine_hhdm_response *hhdm;

extern uint64_t highest_address_memmap;
extern uint8_t *page_bitmap;
extern uint64_t bitmap_size_bytes;

void initPMM(void);
void *pmmClaimContiguousPages(size_t count);
void pmmFreeContiguousPages(void *ptr, size_t count);