#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "limine.h"

// we urgently need some way to track mappings properly in our vmm

/* UC (strong UnCacheable):
 *   - sysmem locations not cached
 *   - all rws appear on bus without reordering
 *   - no speculative mem accesses, pt walks, speculated branch target prefetches
 *   note: use gpr loads and stores, mmio with read side effects
 * UC- (UnCacheable):
 *   - same like UC, but overridable via MTRRs
 *   note: selectable only from PAT, huge areas that are read only
 *         once to avoid evicting cache lines, write-only data structures
 * WC (Write Combining):
 *   - sysmem locations not cached, no coherency enforced
 *   - speculative reads allowed
 *   - writes delayed and combined in the WC buffer
 *   note: frame buffers
 * WT (Write Through):
 *   - sysmem rws cached
 *   - reads come from cache lines on cache hits, read misses cause cache fills
 *   - speculative reads allowed
 *   - writes are written to cache lines and through to sysmem
 *   - memory writes: invalid cache lines never filled, valid cache lines filled or invalidated
 *   - WC allowed
 *   note: non-snooping sysmem accessing devies (processor & sysmem coherent)
 * WB (Write Back):
 *   - sysmem rws cached
 *   - reads come from cache lines on cache hits, read misses cause cache fills
 *   - speculative reads allowed
 *   - write misses cause cache line fills
 *   - writes performed entirely in cache
 *   - WC allowed
 *   - written to sysmem upon write-back operation (cache line dealloc, cache consistency mechanisms)
 *   note: less bus traffic because less sysmem writes, requires devices to snoop, dma with io agent, default
 * WP (Write Protected):
 *   - reads come from cache lines, read misses cause cache fills
 *   - writes propagated to bus and invalidate corresponding cache lines on all processors on bus
 *   - speculative reads allowed */

/* PCD  PWT  PAT    PATNR    Reset  Limine
 * 0    0    0      PAT0     WB     WB
 * 0    0    1      PAT1     WT     WT
 * 0    1    0      PAT2     UC-    UC-
 * 0    1    1      PAT3     UC     UC
 * 1    0    0      PAT4     WB     WP
 * 1    0    1      PAT5     WT     WC
 * 1    1    0      PAT6     UC-    unspecified
 * 1    1    1      PAT7     UC     unspecified */



// The below list is, with exception of 1GiB pages, complete
// and can be used as reference (it's my personal lookup table)

#pragma region page_table_format

// pagemap common format
#define PM_COMMON_PRESENT (1ul << 0)
#define PM_COMMON_WRITE (1ul << 1)
#define PM_COMMON_USER (1ul << 2)
#define PM_COMMON_PWT (1ul << 3)
#define PM_COMMON_PCD (1ul << 4)
#define PM_COMMON_ACCESSED (1ul << 5)
#define PM_COMMON_NX (1ul << 63)    // if IA32_EFER.NXE = 1, otherwise reserved (has to be 0)

// PML4E, PDPTE, PDE
// (common)
// ignored
// PML4E: reserved (has to be 0) / PDPTE: PS: 1 for 1GiB page / PDE: PS: 1 for 2MiB
// [10:8] ignored
// ignored, HLAT
// [M-1:12] phys of pdpt
// [51:M] reserved (has to be 0)
// [62:52] ignored

// PDPTE - 1GiB, PDE - 2MiB page
// (common)
#define PDXX_COMMON_DIRTY (1ul << 6)
#define PDXX_COMMON_PS (1ul << 7)        // set
#define PDXX_COMMON_GLOBAL (1ul << 8)    // if CR4.PGE, else ignored
// [10:9] ignored
// ignored, HLAT
#define PDXX_COMMON_PAT (1ul << 12)
// PDPTE: [20:13] reserved (has to be 0) / PDE: [29:13] reserved (has to be 0)
#define PDPT_PHYS_MASK (0x000fffffc0000000ul)   // [M-1:30]
#define PD_PHYS_MASK (0x000fffffffe00000ul)     // [M-1:21]
// [51:M] reserved (has to be 0)
// [58:52] ignored
// [62:59] prot key, if CR4.PKE = 1 or CR4.PKS = 1 then access rights, else unused

// PTE
// (common)
#define PT_DIRTY (1ul << 6)
#define PT_PAT (1ul << 7)      // set
#define PT_GLOBAL (1ul << 8)   // if CR4.PGE, else ignored
// [10:9] ignored
// ignored, HLAT
#define PT_PHYS_MASK (0x000ffffffffff000ul)     // [M-1:12]
// [51:M] reserved (has to be 0)
// [58:52] ignored
// [62:59] prot key, if CR4.PKE = 1 or CR4.PKS = 1 then access rights, else unused

#define PML_LOWER_MASK (0x000ffffffffff000ul)

#pragma endregion page_table_format



// the following two will change once I add aarch64 support

// memory access flags
#define MAF_WRITE PM_COMMON_WRITE
#define MAF_USER PM_COMMON_USER
#define MAF_NX PM_COMMON_NX

enum memory_cache_type {
    MCT_UNCACHEABLE = 0,
    MCT_WRITE_COMBINING = 1,
    MCT_WRITE_THROUGH = 2,
    MCT_WRITE_BACK = 3,
    MCT_WRITE_PROTECTED = 4
};

typedef struct {
    uintptr_t pml4_address;
    // we really should be tracking allocations for page tables per address space here
} page_map_ctx_t;

extern page_map_ctx_t kernel_pmc;
extern struct limine_kernel_address_response *kernel_address;

void init_vmm(void);

// internal
void mmu_map_single_page_4k(page_map_ctx_t *pmc, uintptr_t va, uintptr_t pa, uint64_t flags);
void mmu_map_single_page_2m(page_map_ctx_t *pmc, uintptr_t va, uintptr_t pa, uint64_t flags);
void mmu_map_single_page_1g(page_map_ctx_t *pmc, uintptr_t va, uintptr_t pa, uint64_t flags);

// use these for mapping stuff
void mmu_map_range_linear(page_map_ctx_t *ctx, uintptr_t vbase, uintptr_t pbase,
    size_t len, uint64_t access_flags, enum memory_cache_type cache_type);

uint64_t *mmu_walk_table(page_map_ctx_t *pmc, uintptr_t va, int *depth, size_t *idx);

uintptr_t virt2phys(page_map_ctx_t *pmc, uintptr_t virt);
void mmu_set_ctx(const page_map_ctx_t *pmc);

// [FIXME] remove/rewrite (also add to uacpi_kernel_unmap)
bool mmu_unmap_single_page(page_map_ctx_t *pmc, uintptr_t va, bool free_pa);