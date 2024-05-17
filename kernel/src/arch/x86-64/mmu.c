#include <stdbool.h>
#include <cpuid.h>
#include <stddef.h>
#include <stdint.h>

#include "mmu.h"
#include "compiler.h"
#include "frame_alloc.h"
#include "interrupt.h"
#include "kprintf.h"
#include "apic.h"
#include "limine.h"
#include "locking.h"
#include "macros.h"
#include "cpu_id.h"
#include "memory.h"

// THIS NEEDS A REWORK

static uint64_t phys_addr_width = 0;
static uint64_t lin_addr_width = 0;

page_map_ctx_t kernel_pmc = { 0x0 };

struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};
struct limine_kernel_address_response *kernel_address;

// get rid of this lock
static k_spinlock_t map_page_lock;

static uint64_t *pml4_to_pt(uint64_t *pml4, uint64_t va, bool force);
static void init_kpm();

void init_vmm(void)
{
    kprintf_verbose("%s initializing mmu...\n", ansi_progress_string);

    struct cpuid_ctx ctx = {.leaf = 0x80000008};
    cpuid(&ctx);
    phys_addr_width = ctx.eax & 0xFF;
    lin_addr_width = (ctx.eax >> 8) & 0xFF;
    kprintf_verbose("  - vmm: phys_addr_width: %lu / lin_addr_width: %lu\n", phys_addr_width, lin_addr_width);

    init_kpm();

    kprintf("%s initialized mmu, set up kernel page tables\n", ansi_okay_string);
}

// [FIXME] this is NOT sufficient or efficient AT ALL
static inline void tlb_flush()
{
    __asm__ volatile (
        "movq %%cr3, %%rax\n\
	    movq %%rax, %%cr3\n"
        : : : "rax"
   );
}

// try  to fin the pagemap. don't allocate on failure.
static uint64_t *attempt_walk_pagemap_single_lvl(uint64_t *pml_pointer, uint64_t index)
{
    if (pml_pointer[index] & PM_COMMON_PRESENT) {
        return (uint64_t *)((pml_pointer[index] & PML_LOWER_MASK) + hhdm->offset);
    }

    // if pml_pointer[index] contains no entry
    return NULL;
}

// walk the page tables by one level, starting at pml_pointer[index]. force
// the traversal by allocating new tables with flags set.
// [TODO] all allocations are tracked in ctx (is this necessary? we could just walk the tables
// and find the pmls addresses this way...)
static uint64_t *force_walk_pagemap_single_lvl(uint64_t *pml_pointer, uint64_t index, uint64_t flags)
{
    if (pml_pointer[index] & PM_COMMON_PRESENT) {
        return (uint64_t *)((pml_pointer[index] & PML_LOWER_MASK) + hhdm->offset);
    }

    // if pml_pointer[index] contains no entry

    // track this in ctx
    void *below_pml = (void *)page2phys(page_calloc(PAGES_1_ORDER));

    pml_pointer[index] = (uint64_t)below_pml | flags;
    return (uint64_t *)((uint64_t)below_pml + hhdm->offset);
}

void mmu_map_single_page_4k(page_map_ctx_t *pmc, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    if (pa & 0xfff || va & 0xfff)
        kpanic(0, NULL, "bad alignment (map 4kib)\n");

    uint64_t *pml4 = (uint64_t *)pmc->pml4_address;

    size_t pml4_index = (va & (0x1fful << 39)) >> 39,
        pdpt_index = (va & (0x1fful << 30)) >> 30,
        pd_index = (va & (0x1fful << 21)) >> 21,
        pt_index = (va & (0x1fful << 12)) >> 12;

    spin_lock_global(&map_page_lock);

    uint64_t *pmlx = force_walk_pagemap_single_lvl(pml4, pml4_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    pmlx = force_walk_pagemap_single_lvl(pmlx, pdpt_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    pmlx = force_walk_pagemap_single_lvl(pmlx, pd_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    pmlx[pt_index] = pa | flags;

    tlb_flush();

    spin_unlock_global(&map_page_lock);
}

void mmu_map_single_page_2m(page_map_ctx_t *pmc, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    if (pa & 0x1fffff || va & 0x1fffff)
        kpanic(0, NULL, "bad alignment (map 2mib)\n");

    uint64_t *pml4 = (uint64_t *)pmc->pml4_address;

    size_t pml4_index = (va & (0x1fful << 39)) >> 39,
        pdpt_index = (va & (0x1fful << 30)) >> 30,
        pd_index = (va & (0x1fful << 21)) >> 21;

    spin_lock_global(&map_page_lock);

    uint64_t *pmlx = force_walk_pagemap_single_lvl(pml4, pml4_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    pmlx = force_walk_pagemap_single_lvl(pmlx, pdpt_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    pmlx[pd_index] = pa | flags | PDXX_COMMON_PS;

    tlb_flush();

    spin_unlock_global(&map_page_lock);
}

void mmu_map_single_page_1g(page_map_ctx_t *pmc, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    if (pa & 0x3fffffff || va & 0x3fffffff)
        kpanic(0, NULL, "bad alignment (map 1gib)\n");

    uint64_t *pml4 = (uint64_t *)pmc->pml4_address;

    size_t pml4_index = (va & (0x1fful << 39)) >> 39,
        pdpt_index = (va & (0x1fful << 30)) >> 30;

    spin_lock_global(&map_page_lock);

    uint64_t *pmlx = force_walk_pagemap_single_lvl(pml4, pml4_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    pmlx[pdpt_index] = pa | flags | PDXX_COMMON_PS;

    tlb_flush();

    spin_unlock_global(&map_page_lock);
}

// map a range from base -> base + len with the biggest mapping sizes available.
// base and len have to be page aligned.
// [FIXME] use custom flags since PAT is not always at the same bit position!!!
void mmu_map_range_linear(page_map_ctx_t *ctx, uintptr_t vbase, uintptr_t pbase,
    size_t len, uint64_t access_flags, enum memory_cache_type cache_type)
{
    if (pbase & 0xfff || vbase & 0xfff || len & 0xfff)
        kpanic(0, NULL, "bad alignment (map 1gib)\n");

    uintptr_t off = DIFF(vbase, pbase);
    uintptr_t lo = pbase, limit = pbase + len;

    uint64_t flags_pt, flags_pdxx;
    flags_pt = flags_pdxx = access_flags | PM_COMMON_PRESENT;
    // cache type
    switch (cache_type)
    {
        case MCT_UNCACHEABLE:
            flags_pt |= PM_COMMON_PWT | PT_PAT;
            flags_pdxx |= PM_COMMON_PWT | PDXX_COMMON_PAT;
            break;
        case MCT_WRITE_BACK:
            break;
        case MCT_WRITE_COMBINING:
            flags_pt |= PM_COMMON_PCD | PT_PAT;
            flags_pdxx |= PM_COMMON_PCD | PDXX_COMMON_PAT;
            break;
        case MCT_WRITE_PROTECTED:
            flags_pt |= PM_COMMON_PCD;
            break;
        case MCT_WRITE_THROUGH:
            flags_pt |= PT_PAT;
            flags_pdxx |= PDXX_COMMON_PAT;
            break;
        default:
            kpanic(0, NULL, "unreachable\n");
    }

    uintptr_t end = MIN(ALIGN_UP(lo, 0x200000), ALIGN_DOWN(limit, 0x1000));
    for (; lo < end; lo += 0x1000) {
        mmu_map_single_page_4k(ctx, lo + off, lo, flags_pt);
    }

    end = MIN(ALIGN_UP(lo, 0x40000000), ALIGN_DOWN(limit, 0x200000));
    for (; lo < end; lo += 0x200000) {
        mmu_map_single_page_2m(ctx, lo + off, lo, flags_pdxx);
    }

    // middle 1gib mappings
    end = ALIGN_DOWN(limit, 0x40000000);
    for (; lo < end; lo += 0x40000000) {
        mmu_map_single_page_1g(ctx, lo + off, lo, flags_pdxx);
    }

    end = ALIGN_DOWN(limit, 0x200000);
    for (; lo < end; lo += 0x200000) {
        mmu_map_single_page_2m(ctx, lo + off, lo, flags_pdxx);
    }

    end = ALIGN_DOWN(limit, 0x1000);
    for (; lo < end; lo += 0x1000) {
        mmu_map_single_page_4k(ctx, lo + off, lo, flags_pt);
    }
}

// depth 1 = 1gib, 2 = 2mib, 3 = 4kib
uint64_t *mmu_walk_table(page_map_ctx_t *pmc, uintptr_t va, int *depth, size_t *idx)
{
    uint64_t *pml4 = (uint64_t *)pmc->pml4_address;

    size_t pml4_index = (va & (0x1fful << 39)) >> 39,
        pdpt_index = (va & (0x1fful << 30)) >> 30,
        pd_index = (va & (0x1fful << 21)) >> 21,
        pt_index = (va & (0x1fful << 12)) >> 12;

    spin_lock_global(&map_page_lock);

    (*depth) = 0;

    uint64_t *pdpt = attempt_walk_pagemap_single_lvl(pml4, pml4_index);
    if (!pdpt) {
        spin_unlock_global(&map_page_lock);
        return NULL;
    }
    (*depth)++;
    (*idx) = pdpt_index;
    if (pdpt[pdpt_index] & PDXX_COMMON_PS) {
        spin_unlock_global(&map_page_lock);
        return pdpt;
    }

    uint64_t *pd = attempt_walk_pagemap_single_lvl(pdpt, pdpt_index);
    if (!pd) {
        spin_unlock_global(&map_page_lock);
        return NULL;
    }
    (*depth)++;
    (*idx) = pd_index;
    if (pd[pd_index] & PDXX_COMMON_PS) {
        spin_unlock_global(&map_page_lock);
        return pd;
    }

    uint64_t *pt = attempt_walk_pagemap_single_lvl(pd, pd_index);
    if (!pt) {
        spin_unlock_global(&map_page_lock);
        return NULL;
    }
    (*depth)++;
    (*idx) = pt_index;
    if (pt[pt_index] & PM_COMMON_PRESENT) {
        spin_unlock_global(&map_page_lock);
        return pt;
    }

    tlb_flush();

    spin_unlock_global(&map_page_lock);

    return NULL;
}

// return NULL if requested PT doesn't exist and shouldn't be allocated
static uint64_t *pml4_to_pt(uint64_t *pml4, uint64_t va, bool force)
{
    (void)force;

    size_t pml4_index = (va & (0x1fful << 39)) >> 39,
        pdpt_index = (va & (0x1fful << 30)) >> 30,
        pd_index = (va & (0x1fful << 21)) >> 21;

    uint64_t *pmlx = force_walk_pagemap_single_lvl(pml4, pml4_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    pmlx = force_walk_pagemap_single_lvl(pmlx, pdpt_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    pmlx = force_walk_pagemap_single_lvl(pmlx, pd_index,
        PM_COMMON_PRESENT | PM_COMMON_WRITE);

    return pmlx;
}

// return 1 if successful, 0 if nothing got unmapped
bool mmu_unmap_single_page(page_map_ctx_t *pmc, uintptr_t va, bool free_pa)
{
    (void)pmc;
    (void)va;
    (void)free_pa;

    /*spin_lock_global(&map_page_lock);
    size_t pt_index = (va & (0x1fful << 12)) >> 12;
    uint64_t *pt = pml4_to_pt((uint64_t *)pmc->pml4_address, va, false);
    if (pt == NULL) {
        spin_unlock_global(&map_page_lock);
        return false;
    }

    // free page
    if (free_pa) {
        page_free_temp((void *)(pt[pt_index] & PML_LOWER_MASK), 1);
    }

    // unmap page
    pt[pt_index] = 0;

    tlb_flush();

    spin_unlock_global(&map_page_lock);*/

    return true;
}

uintptr_t virt2phys(page_map_ctx_t *pmc, uintptr_t virt)
{
    (void)pmc;

    int depth = -1;
    size_t index = 0;
    uint64_t *pmlx = mmu_walk_table(&kernel_pmc, virt, &depth, &index);
    if (!pmlx) return 0x0;

    if (depth == 1) {
        // 1gib
        return (pmlx[index] & (((1ull << phys_addr_width) - 1) & ~0x3fffffff)) | (virt & 0x3fffffff);
    } else if (depth == 2) {
        // 2mib
        return (pmlx[index] & (((1ull << phys_addr_width) - 1) & ~0x1fffff)) | (virt & 0x1fffff);
    } else if (depth == 3) {
        // 4kib
        return (pmlx[index] & (((1ull << phys_addr_width) - 1) & ~0xfff)) | (virt & 0xfff);
    }

    kpanic(0, NULL, "unreachable\n");
    unreachable();
}

static void init_kpm(void)
{
    // [FIXME] for some reason, this is INCREDIBLY slow on real hardware.
    // try mapping with bigger page sizes.

    if (!kernel_address_request.response) {
        kpanic(0, NULL, "limine kernel address request not answered\n\r");
    }

    // claim space for pml4
    kernel_pmc.pml4_address = (uintptr_t)page_alloc_temp(psize2order(1 * PAGE_SIZE));
    if (!kernel_pmc.pml4_address) {
        kpanic(0, NULL, "pmm_claim_contiguous_pages returned NULL\n\r");
    }
    kernel_pmc.pml4_address += hhdm->offset;
    // zero out contents of newly allocated page
    memset((void *)kernel_pmc.pml4_address, 0, PAGE_SIZE);

    // Lyre's approach of using linker symbols
    extern linker_symbol_ptr text_start_addr, text_end_addr,
        rodata_start_addr, rodata_end_addr,
        data_start_addr, data_end_addr;

    uintptr_t text_start = (uintptr_t)text_start_addr,
        rodata_start = (uintptr_t)rodata_start_addr,
        data_start = (uintptr_t)data_start_addr,
        text_end = (uintptr_t)text_end_addr,
        rodata_end = (uintptr_t)rodata_end_addr,
        data_end = (uintptr_t)data_end_addr;

    if (!kernel_address_request.response) {
        kpanic(0, NULL, "Kernel Address request failed!\n");
    }

    // limine
    kernel_address = kernel_address_request.response;
    mmu_map_single_page_4k(&kernel_pmc, ALIGN_DOWN((lapic_address + hhdm->offset), PAGE_SIZE),
        ALIGN_DOWN(lapic_address, PAGE_SIZE), PM_COMMON_PRESENT | PM_COMMON_WRITE);
    lapic_address += hhdm->offset;

    // map early_mem allocator data structures
    for (size_t i = 0; i < early_mem_allocations; i++) {
        struct early_mem_alloc_mapping *mapping = &early_mem_mappings[i];

        for (size_t off = ALIGN_DOWN(mapping->start, PAGE_SIZE); off < ALIGN_UP(mapping->start + mapping->length, PAGE_SIZE); off += PAGE_SIZE) {
            mmu_map_single_page_4k(&kernel_pmc, off + hhdm->offset, off, PM_COMMON_PRESENT | PM_COMMON_WRITE);
        }
    }

    for (size_t i = 0; i < memmap_entry_count; i++) {
        struct memmap_entry *entry = &memmap[i];

        // [DBG]
        //kprintf("Entry %-2lu: Base = 0x%0p, End = 0x%p, Length = %lu pages, Type = %lu\n\r",
        //    i, entry->start, entry->start + entry->length, entry->length / PAGE_SIZE, entry->type);

        // direct map usable entries for now
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            // alignment is guaranteed
            mmu_map_range_linear(&kernel_pmc, entry->start + hhdm->offset, entry->start,
                entry->length, MAF_WRITE, MCT_WRITE_BACK);
        }
        // framebuffer
        else if (entry->type == LIMINE_MEMMAP_FRAMEBUFFER) {
            mmu_map_range_linear(&kernel_pmc, ALIGN_DOWN(entry->start + hhdm->offset, PAGE_SIZE),
                ALIGN_DOWN(entry->start, PAGE_SIZE), ALIGN_UP(entry->length, PAGE_SIZE),
                MAF_NX | MAF_WRITE, MCT_WRITE_COMBINING);
                // [FIXME] pat!!!
        }
        // bootloader reclaimable
        else if (entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            // alignment is guaranteed
            mmu_map_range_linear(&kernel_pmc, entry->start + hhdm->offset, entry->start,
                entry->length, MAF_WRITE, MCT_WRITE_BACK);
        }
    }

    mmu_map_range_linear(&kernel_pmc, text_start,
        text_start - kernel_address->virtual_base + kernel_address->physical_base,
        text_end - text_start, 0, MCT_WRITE_BACK);

    mmu_map_range_linear(&kernel_pmc, rodata_start,
        rodata_start - kernel_address->virtual_base + kernel_address->physical_base,
        rodata_end - rodata_start, MAF_NX, MCT_WRITE_BACK);

    mmu_map_range_linear(&kernel_pmc, data_start,
        data_start - kernel_address->virtual_base + kernel_address->physical_base,
        data_end - data_start, MAF_WRITE | MAF_NX, MCT_WRITE_BACK);

    mmu_set_ctx(&kernel_pmc);
    tlb_flush();
}

inline void mmu_set_ctx(const page_map_ctx_t *pmc)
{
    __asm__ volatile (
        "movq %0, %%cr3\n"
        : : "r" ((uint64_t *)(pmc->pml4_address - hhdm->offset)) : "memory"
    );
}