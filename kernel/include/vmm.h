#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "macros.h"

void init_vmm(void);

typedef struct {
    uintptr_t pml4_address;
} page_map_ctx;

extern page_map_ctx kernel_pmc;

// page directory entry macros
#define PTE_BIT_PRESENT (1ul << 0ul)
#define PTE_BIT_READ_WRITE (1ul << 1ul)
#define PTE_BIT_ACCESS_ALL (1ul << 2ul)
#define PTE_BIT_WRITE_THROUGH_CACHING (1ul << 3ul)
#define PTE_BIT_DISABLE_CACHING (1ul << 4ul)
#define PTE_BIT_PDE_OR_PTE_ACCESSED (1ul << 5ul)
#define PTE_BIT_DIRTY (1ul << 6ul)
#define PTE_BIT_PAT_SUPPORTED (1ul << 7ul)
#define PTE_BIT_GLOBAL (1ul << 8ul)
#define PTE_BIT_EXECUTE_DISABLE (1ul << 63ul)

/*
 * PML4 (Page Map Level 4):
 *  - level 4
 *  - bits 39 - 47
 *  - entries point to PDPTs
 *  - address stored in cr3
 * 
 * PDPT (Page Directory Pointer Table):
 *  - level 3
 *  - bits 30 - 38
 *  - entries points to PDs
 * 
 * PD (Page Directories):
 *  - level 2
 *  - bits 21 - 29
 *  - entries points to PTs
 * 
 * PT (Page Table):
 *  - level 1
 *  - bits 12 - 20
 *  - entries refer to pages
 * 
 * Individual Page:
 *  - level 0
 *  - bits 0 - 11
*/
void vmm_map_single_page(page_map_ctx *pmc, uintptr_t va, uintptr_t pa, uint64_t flags);
bool vmm_unmap_single_page(page_map_ctx *pmc, uintptr_t va, bool free_pa);

void vmm_set_ctx(const page_map_ctx *pmc);