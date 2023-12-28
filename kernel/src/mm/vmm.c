#include "mm/vmm.h"
#include "mm/pmm.h"
#include "limine.h"
#include "std/kprintf.h"
#include "vmm.h"
#include <stdbool.h>
#include "std/typedefs.h"

page_map_ctx kernel_pmc = { 0x0 };

extern struct limine_hhdm_response *hhdm;
struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};
struct limine_kernel_address_response *kernel_address;

static uint64_t *getBelowPML(uint64_t *pml_pointer, uint64_t index, bool force);
static uint64_t *pml4ToPT(uint64_t *pml4, uint64_t va, bool force);
static void initKPM();

void initVMM(void)
{
    initKPM();
}

static inline void tlbFlush()
{
    asm volatile (
        "move"
    )
}

static uint64_t *getBelowPML(uint64_t *pml_pointer, uint64_t index, bool force)
{
    //Bprintf("here... index = %lu\n", index);
    if (pml_pointer[index] & PTE_BIT_PRESENT) {
        /*
         * With 4KB pages, PTE[12] - PTE[51] contain the PA of the next lower level
         * See Intel SDM Table 4-20 (vol 3, 4.5.5)
        */
        //Bprintf("Returning1... lower = 0x%016lX\n", (EXTRACT_BITS(pml_pointer[index], 12, 51)));
        return (uint64_t *)(EXTRACT_BITS(pml_pointer[index], 12, 51) + hhdm->offset);
    }
    //Bprintf("Returning2...\n");
    // if pml_pointer[index] contains no entry
    
    if (!force) {
        return NULL;
    }

    void *below_pml = pmmClaimContiguousPages(1);
    if (below_pml == NULL) {
        // [DBG]
        printf("Allocating pages for vmm tables failed\n\r");
        asm ("cli\n hlt\n");
    }

    // the most strict permissions are used, so
    // before the lowest level allow "everything"
    // [FIXME] shift 12 to the left because (intel sdm -> above)
    pml_pointer[index] = (uint64_t)below_pml << 12 | PTE_BIT_PRESENT | PTE_BIT_READ_WRITE | PTE_BIT_ACCESS_ALL;
    return (uint64_t *)((uint64_t)below_pml + hhdm->offset);
}

// return NULL if requested PT doesn't exist and shouldn't be allocated
static uint64_t *pml4ToPT(uint64_t *pml4, uint64_t va, bool force)
{
    size_t pml4_index = EXTRACT_BITS(va, 39ul, 47ul),
        pdpt_index = EXTRACT_BITS(va, 30ul, 38ul),
        pd_index = EXTRACT_BITS(va, 21ul, 29ul);

    uint64_t *pdpt = NULL,
        *pd = NULL,
        *pt = NULL;

    pdpt = getBelowPML(pml4, pml4_index, force);
    if (!pdpt) {
        return NULL;
    }
    //Bprintf("pdpt address: 0x%lX\n", &pdpt[0]);
    pd = getBelowPML(pdpt, pdpt_index, force);
    if (!pd) {
        return NULL;
    }
    //Bprintf("pd address: 0x%lX\n", &pd[0]);
    pt = getBelowPML(pd, pd_index, force);
    if (!pt) {
        return NULL;
    }

    return (uint64_t *)pt;
}

static void initKPM(void)
{
    // limine kernel address request
    // struct limine_kernel_address_response {
    // uint64_t revision;
    // uint64_t physical_base;
    // uint64_t virtual_base;
    // };
    if (!kernel_address_request.response) {
        // [DBG]
        printf("limine kernel address request not answered\n\r");
        asm ("cli\n hlt\n");
    }

    // claim space for pml4
    kernel_pmc.pml4_address = (uintptr_t)pmmClaimContiguousPages(1);
    if (!kernel_pmc.pml4_address) {
        // [DBG]
        printf("vmm.c: kernel_pmc.pml4_address = pmmContiguousPages(1); returned NULL\n\r");
        asm ("cli\n hlt\n");
    }
    kernel_pmc.pml4_address += hhdm->offset;

    // [TODO] meanwhile take Lyre's approach of using linker symbols
    extern linker_symbol_ptr text_start_addr, text_end_addr,
        rodata_start_addr, rodata_end_addr,
        data_start_addr, data_end_addr;

    printf("text_start_addr = 0x%016lX\n", text_start_addr);
    printf("text_end_addr = 0x%016lX\n", text_end_addr);
    printf("rodata_start_addr = 0x%016lX\n", rodata_start_addr);
    printf("rodata_end_addr = 0x%016lX\n", rodata_end_addr);
    printf("data_start_addr = 0x%016lX\n", data_start_addr);
    printf("data_end_addr = 0x%016lX\n", data_end_addr);

    uintptr_t text_start = ALIGN_DOWN((uintptr_t)text_start_addr, PAGE_SIZE),
        rodata_start = ALIGN_DOWN((uintptr_t)rodata_start_addr, PAGE_SIZE),
        data_start = ALIGN_DOWN((uintptr_t)data_start_addr, PAGE_SIZE),
        text_end = ALIGN_UP((uintptr_t)text_end_addr, PAGE_SIZE),
        rodata_end = ALIGN_UP((uintptr_t)rodata_end_addr, PAGE_SIZE),
        data_end = ALIGN_UP((uintptr_t)data_end_addr, PAGE_SIZE);

    if (!kernel_address_request.response) {
        printf("Kernel Address request failed!\n");
        asm volatile("cli\n hlt\n");
    }
    struct limine_kernel_address_response *ka = kernel_address_request.response;

    for (uintptr_t text_addr = text_start; text_addr < text_end; text_addr += PAGE_SIZE) {
        uintptr_t phys = text_addr - ka->virtual_base + ka->physical_base;
        //Bprintf("calling mapPage phys = 0x%lX virt = 0x%lX\n", phys, text_addr);
        mapPage(&kernel_pmc, text_addr, phys, PTE_BIT_PRESENT);
        //Bprintf("done\n");
    }

    for (uintptr_t rodata_addr = rodata_start; rodata_addr < rodata_end; rodata_addr += PAGE_SIZE) {
        uintptr_t phys = rodata_addr - ka->virtual_base + ka->physical_base;
        mapPage(&kernel_pmc, rodata_addr, phys, PTE_BIT_PRESENT | PTE_BIT_EXECUTE_DISABLE);
    }

    for (uintptr_t data_addr = data_start; data_addr < data_end; data_addr += PAGE_SIZE) {
        uintptr_t phys = data_addr - ka->virtual_base + ka->physical_base;
        mapPage(&kernel_pmc, data_addr, phys, PTE_BIT_PRESENT | PTE_BIT_READ_WRITE | PTE_BIT_EXECUTE_DISABLE);
    }
}

// maps a page size aligned VA to a page size aligned PA.
// [TODO] tlb shootdown
void mapPage(page_map_ctx *pmc, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    size_t pt_index = EXTRACT_BITS(va, 12ul, 20ul);
    //Bprintf("calling pml4ToPT\n");
    uint64_t *pt = pml4ToPT((uint64_t *)pmc->pml4_address, va, true);
    if (pt == NULL) {
        // [DBG]
        printf("Page Mapping couldnt be made (pt doesn't exist and didnt get created)\n\r");
        asm ("cli\n hlt\n");
    }
    // map page
    // [FIXME] << 12 ?
    pt[pt_index] = pa << 12 | flags;
}

// [TODO] tlb shootdown
void removePageMapping(page_map_ctx *pmc, uintptr_t va)
{
    size_t pt_index = EXTRACT_BITS(va, 12ul, 20ul);
    uint64_t *pt = pml4ToPT((uint64_t *)pmc->pml4_address, va, false);
    if (pt == NULL) {
        // [DBG]
        printf("Page Mapping couldnt be removed (pt doesn't exist)\n\r");
        asm ("cli\n hlt\n");
    }
    // unmap page
    pt[pt_index] = 0x0;
}

inline void setCtxToPM(page_map_ctx *pmc)
{
    asm volatile (
        "movq %0, %%cr3\n"
        : : "r" (pmc->pml4_address - hhdm->offset) : "memory"
    );
}