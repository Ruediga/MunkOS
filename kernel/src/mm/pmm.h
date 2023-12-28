#pragma once

#include <limine.h>
#include <stdint.h>
#include <stddef.h>

// 4096 byte pages
#define PAGE_SIZE 0x1000

void initPMM(void);
void *pmmClaimContiguousPages(size_t count);
void pmmFreeContiguousPages(void *ptr, size_t count);