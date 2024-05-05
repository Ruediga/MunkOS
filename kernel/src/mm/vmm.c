#include "vmm.h"
#include "frame_alloc.h"
#include "kprintf.h"
#include "memory.h"
#include "cpu.h"
#include "apic.h"

#include <stdbool.h>
#include <cpuid.h>

page_map_ctx kernel_pmc = { 0x0 };
static uint64_t phys_addr_width = 0;
static uint64_t lin_addr_width = 0;

struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};
struct limine_kernel_address_response *kernel_address;

static k_spinlock_t map_page_lock;

static uint64_t *get_below_pml(uint64_t *pml_pointer, uint64_t index, bool force);
static uint64_t *pml4_to_pt(uint64_t *pml4, uint64_t va, bool force);
static void init_kpm();

void init_vmm(void)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    // leaf 0x80000008: processor information
    __get_cpuid(0x80000008, &eax, &ebx, &ecx, &edx);
    phys_addr_width = eax & 0xFF;
    lin_addr_width = (eax >> 8) & 0xFF;
    kprintf("  - vmm: phys_addr_width: %lu / lin_addr_width: %lu\n", phys_addr_width, lin_addr_width);

    init_kpm();
}

static inline void tlb_flush()
{
    __asm__ volatile (
        "movq %%cr3, %%rax\n\
	    movq %%rax, %%cr3\n"
        : : : "rax"
   );
}

static uint64_t *get_below_pml(uint64_t *pml_pointer, uint64_t index, bool force)
{
    if ((pml_pointer[index] & PTE_BIT_PRESENT) != 0) {
        // With 4KB pages, PTE[12] - PTE[51] contain the PA of the next lower level
        // See Intel SDM Table 4-20 (vol 3, 4.5.5)
        return (uint64_t *)((pml_pointer[index] & 0x000ffffffffff000) + hhdm->offset);
    }
    // if pml_pointer[index] contains no entry

    if (!force) {
        return NULL;
    }

    void *below_pml = (void *)page2phys(page_alloc(psize2order(1 * PAGE_SIZE)));
    if (below_pml == NULL) {
        kpanic(0, NULL, "Allocating pages for vmm tables failed\n\r");
    }
    // zero out contents of newly allocated page
    memset((void *)((uint64_t)below_pml + hhdm->offset), 0, PAGE_SIZE);

    // the most strict permissions are used, so
    // before the lowest level allow "everything"
    pml_pointer[index] = (uint64_t)below_pml | PTE_BIT_PRESENT | PTE_BIT_READ_WRITE | PTE_BIT_ACCESS_ALL;
    return (uint64_t *)((uint64_t)below_pml + hhdm->offset);
}

// return NULL if requested PT doesn't exist and shouldn't be allocated
static uint64_t *pml4_to_pt(uint64_t *pml4, uint64_t va, bool force)
{
    size_t pml4_index = (va & (0x1fful << 39)) >> 39,
        pdpt_index = (va & (0x1fful << 30)) >> 30,
        pd_index = (va & (0x1fful << 21)) >> 21;

    uint64_t *pdpt = NULL,
        *pd = NULL,
        *pt = NULL;

    //Bprintf("pml4_index: %lu\n", pml4_index);
    pdpt = get_below_pml(pml4, pml4_index, force);
    if (!pdpt) {
        return NULL;
    }
    //Bprintf("pdpt address: 0x%lX\n", &pdpt[0]);
    pd = get_below_pml(pdpt, pdpt_index, force);
    if (!pd) {
        return NULL;
    }
    pt = get_below_pml(pd, pd_index, force);
    if (!pt) {
        return NULL;
    }

    return pt;
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

    uintptr_t text_start = ALIGN_DOWN((uintptr_t)text_start_addr, PAGE_SIZE),
        rodata_start = ALIGN_DOWN((uintptr_t)rodata_start_addr, PAGE_SIZE),
        data_start = ALIGN_DOWN((uintptr_t)data_start_addr, PAGE_SIZE),
        text_end = ALIGN_UP((uintptr_t)text_end_addr, PAGE_SIZE),
        rodata_end = ALIGN_UP((uintptr_t)rodata_end_addr, PAGE_SIZE),
        data_end = ALIGN_UP((uintptr_t)data_end_addr, PAGE_SIZE);

    if (!kernel_address_request.response) {
        kpanic(0, NULL, "Kernel Address request failed!\n");
    }

    // limine
    kernel_address = kernel_address_request.response;

    // map lapics (src/apic/lapic.h)
    vmm_map_single_page(&kernel_pmc, ALIGN_DOWN((lapic_address + hhdm->offset), PAGE_SIZE),
        ALIGN_DOWN(lapic_address, PAGE_SIZE), PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
    lapic_address += hhdm->offset;

    // map early_mem allocator data structures
    for (size_t i = 0; i < early_mem_allocations; i++) {
        struct early_mem_alloc_mapping *mapping = &early_mem_mappings[i];

        for (size_t off = mapping->start; off < ALIGN_UP(mapping->start + mapping->length, PAGE_SIZE); off += PAGE_SIZE) {
            vmm_map_single_page(&kernel_pmc, off + hhdm->offset, off, PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
        }
    }

    for (size_t i = 0; i < memmap_entry_count; i++) {
        struct memmap_entry *entry = &memmap[i];

        // [DBG]
        //kprintf("Entry %-2lu: Base = 0x%0p, End = 0x%p, Length = %lu pages, Type = %lu\n\r",
        //    i, entry->start, entry->start + entry->length, entry->length / PAGE_SIZE, entry->type);

        // direct map usable entries for now
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (size_t off = entry->start; off < entry->start + entry->length; off += PAGE_SIZE) {
                vmm_map_single_page(&kernel_pmc, off + hhdm->offset, off, PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
            }
        }
        // framebuffer
        else if (entry->type ==LIMINE_MEMMAP_FRAMEBUFFER) {
            for (size_t off = 0; off < ALIGN_UP(entry->start + entry->length, PAGE_SIZE); off += PAGE_SIZE) {
                uintptr_t base_off_aligned = ALIGN_UP(entry->start + off, PAGE_SIZE);
                vmm_map_single_page(&kernel_pmc, base_off_aligned + hhdm->offset, base_off_aligned,
                    PTE_BIT_PRESENT | PTE_BIT_READ_WRITE | PTE_BIT_EXECUTE_DISABLE | PTE_BIT_WRITE_THROUGH_CACHING);
            }
        }
        // bootloader reclaimable
        else if (entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            for (size_t off = 0; off < ALIGN_UP(entry->start + entry->length, PAGE_SIZE); off += PAGE_SIZE) {
                vmm_map_single_page(&kernel_pmc, entry->start + off + hhdm->offset, entry->start + off, PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
            }
        }
    }

    for (uintptr_t text_addr = text_start; text_addr < text_end; text_addr += PAGE_SIZE) {
        uintptr_t phys = text_addr - kernel_address->virtual_base + kernel_address->physical_base;
        vmm_map_single_page(&kernel_pmc, text_addr, phys, PTE_BIT_PRESENT);
    }

    for (uintptr_t rodata_addr = rodata_start; rodata_addr < rodata_end; rodata_addr += PAGE_SIZE) {
        uintptr_t phys = rodata_addr - kernel_address->virtual_base + kernel_address->physical_base;
        vmm_map_single_page(&kernel_pmc, rodata_addr, phys, PTE_BIT_PRESENT | PTE_BIT_EXECUTE_DISABLE);
    }

    for (uintptr_t data_addr = data_start; data_addr < data_end; data_addr += PAGE_SIZE) {
        uintptr_t phys = data_addr - kernel_address->virtual_base + kernel_address->physical_base;
        vmm_map_single_page(&kernel_pmc, data_addr, phys, PTE_BIT_PRESENT | PTE_BIT_READ_WRITE | PTE_BIT_EXECUTE_DISABLE);
    }

    vmm_set_ctx(&kernel_pmc);
    tlb_flush();
}

// [TODO] free unused pages holding page tables
// and write a proper range based allocator because wtf is this
// maps a page size aligned VA to a page size aligned PA
void vmm_map_single_page(page_map_ctx *pmc, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    spin_lock(&map_page_lock);
    size_t pt_index = EXTRACT_BITS(va, 12ul, 20ul);
    uint64_t *pt = pml4_to_pt((uint64_t *)pmc->pml4_address, va, true);
    if (pt == NULL) {
        kpanic(0, NULL, "Page Mapping couldnt be made (pt doesn't exist and didnt get created)\n\r");
    }
    // map page
    pt[pt_index] = pa | flags;

    tlb_flush();

    spin_unlock(&map_page_lock);
}

// return 1 if successful, 0 if nothing got unmapped
bool vmm_unmap_single_page(page_map_ctx *pmc, uintptr_t va, bool free_pa)
{
    spin_lock(&map_page_lock);
    size_t pt_index = (va & (0x1fful << 12)) >> 12;
    uint64_t *pt = pml4_to_pt((uint64_t *)pmc->pml4_address, va, false);
    if (pt == NULL) {
        spin_unlock(&map_page_lock);
        return false;
    }

    // free page
    if (free_pa) {
        page_free_temp((void *)(pt[pt_index] & 0x000ffffffffff000), 1);
    }

    // unmap page
    pt[pt_index] = 0;

    tlb_flush();

    spin_unlock(&map_page_lock);

    return true;
}

uintptr_t virt2phys(page_map_ctx *pmc, uintptr_t virt)
{
    size_t pt_index = (virt & (0x1fful << 12)) >> 12;
    uint64_t *pt = pml4_to_pt((uint64_t *)pmc->pml4_address, virt, false);
    if (!pt) return (uintptr_t)NULL;
    uint64_t mask = ((1ull << phys_addr_width) - 1) & ~0xFFF;

    return (pt[pt_index] & mask) | (virt & 0xFFF);
}

inline void vmm_set_ctx(const page_map_ctx *pmc)
{
    __asm__ volatile (
        "movq %0, %%cr3\n"
        : : "r" ((uint64_t *)(pmc->pml4_address - hhdm->offset)) : "memory"
    );
}