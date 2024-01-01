#include "limine.h"
#include "dynmem/kheap.h"
#include "std/macros.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

uint64_t kernel_heap_max_size_pages = 0x0;
uintptr_t kernel_heap_base_address = 0x0;

extern uint64_t highest_address_memmap;
extern struct limine_hhdm_response *hhdm;

void initializeKernelHeap(void)
{
    // place the heap right after the direct map (if full) in (virtual) memory
    kernel_heap_base_address = ALIGN_UP(highest_address_memmap + hhdm->offset, PAGE_SIZE) + PAGE_SIZE;

    // MAX_KERNEL_HEAP_SIZE_PAGES
    kernel_heap_max_size_pages = 0xFF;

    // port liballoc and write functions to allocate / free kernel heap memory
    // maybe heap bitmap to know where to map physical pages when stuff gets freed