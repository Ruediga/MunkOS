#include "limine.h"
#include "dynmem/kheap.h"
#include "std/macros.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "std/memory.h"
#include "std/kprintf.h"

uint64_t kernel_heap_max_size_pages = 0x0;
uintptr_t kernel_heap_base_address = 0x0;
uint8_t *kernel_heap_bitmap = 0x0;

void initializeKernelHeap(size_t max_heap_size_pages)
{
    // place the heap right after the direct map (if full) in (virtual) memory
    kernel_heap_base_address = ALIGN_UP(pmm_highest_address_memmap + hhdm->offset, PAGE_SIZE);

    kernel_heap_max_size_pages = max_heap_size_pages;

    // port liballoc and write functions to allocate / free kernel heap memory
    // maybe heap bitmap to know where to map physical pages when stuff gets freed

    // take one direct mapped page and put a bitmap there to store which pages in the bitmap are taken
    kernel_heap_bitmap = pmmClaimContiguousPages(DIV_ROUNDUP(DIV_ROUNDUP(kernel_heap_max_size_pages, PAGE_SIZE), 8));
    if (!kernel_heap_bitmap) {
        kprintf("no memory (for kernel heap bitmap)\n");
        asm volatile("cli\n hlt");
    }
    kernel_heap_bitmap += hhdm->offset;
    memset(kernel_heap_bitmap, 0x00, PAGE_SIZE);
}

// returns a single page mapped to (page aligned) address
void *getPageAt(uintptr_t address)
{
    if (BITMAP_READ_BIT(kernel_heap_bitmap, (address - kernel_heap_base_address) / PAGE_SIZE) != 0) {
        kprintf("trying to allocate heap page that was never freed\n");
        asm volatile("cli\n hlt");
    }
    void *new_page = pmmClaimContiguousPages(1);
    if (new_page == NULL) {
        return NULL;
    }
    mapPage(&kernel_pmc, address, (uintptr_t)new_page, PTE_BIT_PRESENT | PTE_BIT_EXECUTE_DISABLE | PTE_BIT_READ_WRITE);
    BITMAP_SET_BIT(kernel_heap_bitmap, (address - kernel_heap_base_address) / PAGE_SIZE);

    return (void *)address;
}

// unmaps and deallocates a page at a (page aligned) va
void *returnPageAt(uintptr_t address)
{
    if (BITMAP_READ_BIT(kernel_heap_bitmap, (address - kernel_heap_base_address) / PAGE_SIZE) == 0) {
        return 0;
    }
    // remove it from heap bitmap too
    BITMAP_UNSET_BIT(kernel_heap_bitmap, (address - kernel_heap_base_address) / PAGE_SIZE);

    // this also frees which may be dumb but idgaf
    removePageMapping(&kernel_pmc, address, 1);

    return 0;
}

/** This function is supposed to lock the memory data structures. It
 * could be as simple as disabling interrupts or acquiring a spinlock.
 * It's up to you to decide.
 *
 * \return 0 if the lock was acquired successfully. Anything else is
 * failure.
 */
extern int liballoc_lock() {
    return 0; // for now
}

/** This function unlocks what was previously locked by the liballoc_lock
 * function.  If it disabled interrupts, it enables interrupts. If it
 * had acquiried a spinlock, it releases the spinlock. etc.
 *
 * \return 0 if the lock was successfully released.
 */
extern int liballoc_unlock() {
    return 0; // for now
}

/** This is the hook into the local system which allocates pages. It
 * accepts an integer parameter which is the number of pages
 * required.  The page size was set up in the liballoc_init function.
 *
 * \return NULL if the pages were not allocated.
 * \return A pointer to the allocated memory.
 */
extern void* liballoc_alloc(int c) {
    size_t count = c;
    size_t idx = 0, pages_found = 0;
    // while not at last page (last idx = total - 1)
    while (idx < kernel_heap_max_size_pages) {
        // if free page at idx
        if (BITMAP_READ_BIT(kernel_heap_bitmap, idx) == 0) {
            pages_found++;
            // found enough contiguous pages
            if (pages_found == count) {
                // alloc them
                for (size_t j = (idx + 1) - count; j <= idx; j++) {
                    if (!getPageAt(kernel_heap_base_address + (j * PAGE_SIZE))) {
                        return NULL;
                    }
                }
                // [DBG]
                //printf("Allocated %lu page(s) in the kernel heap bitmap at 0x%016lX\n", count, ((((idx + 1ul) - count) * PAGE_SIZE) + kernel_heap_base_address));
                return (void *)((((idx + 1ul) - count) * PAGE_SIZE) + kernel_heap_base_address);
            }
        // the region we are at doesn't contain enough pages
        } else {
            pages_found = 0;
        }
        idx++;
    }

    // [DBG]
    kprintf("Kernel heap requested pages: %lu not available\n", count);
    return NULL;
}

/** This frees previously allocated memory. The void* parameter passed
 * to the function is the exact same value returned from a previous
 * liballoc_alloc call.
 *
 * The integer value is the number of pages to free.
 *
 * \return 0 if the memory was successfully freed.
 */
extern int liballoc_free(void* address, int count) {
    for (int i = 0; i < count; i++) {
        returnPageAt(((uintptr_t)address + (i * PAGE_SIZE)));
    }
    // [DBG]
    //printf("Freed %i pages from kernel heap at 0x%016lX\n", count, address);

    return 1;
}