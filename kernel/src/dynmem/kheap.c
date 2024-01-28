#include "limine.h"
#include "kheap.h"
#include "macros.h"
#include "vmm.h"
#include "pmm.h"
#include "memory.h"
#include "kprintf.h"
#include "interrupt.h"

uint64_t kernel_heap_max_size_pages = 0x0;
uintptr_t kernel_heap_base_address = 0x0;
uint8_t *kernel_heap_bitmap = 0x0;

static k_spinlock_t malloc_lock;

void init_kernel_heap(size_t max_heap_size_pages)
{
    // place the heap right after the direct map (if full) in (virtual) memory
    kernel_heap_base_address = ALIGN_UP(pmm_highest_address_memmap + hhdm->offset, PAGE_SIZE);

    kernel_heap_max_size_pages = max_heap_size_pages;

    // take one direct mapped page and put a bitmap there to store which pages in the bitmap are taken
    kernel_heap_bitmap = pmm_claim_contiguous_pages(DIV_ROUNDUP(DIV_ROUNDUP(kernel_heap_max_size_pages, PAGE_SIZE), 8));
    if (!kernel_heap_bitmap) {
        kpanic(NULL, "no memory (for kernel heap bitmap)\n");
    }
    kernel_heap_bitmap += hhdm->offset;
    memset(kernel_heap_bitmap, 0x00, PAGE_SIZE);

    kprintf("  - kheap: %lu MiB dynamic memory available\n", kernel_heap_max_size_pages / (1024 * 1024));
}

// returns a single page mapped to (page aligned) address
void *get_page_at(uintptr_t address)
{
    if (BITMAP_READ_BIT(kernel_heap_bitmap, (address - kernel_heap_base_address) / PAGE_SIZE) != 0) {
        kpanic(NULL, "trying to allocate heap page that was never freed\n");
    }
    void *new_page = pmm_claim_contiguous_pages(1);
    if (new_page == NULL) {
        return NULL;
    }
    vmm_map_single_page(&kernel_pmc, address, (uintptr_t)new_page, PTE_BIT_PRESENT | PTE_BIT_EXECUTE_DISABLE | PTE_BIT_READ_WRITE);
    BITMAP_SET_BIT(kernel_heap_bitmap, (address - kernel_heap_base_address) / PAGE_SIZE);

    return (void *)address;
}

// unmaps and deallocates a page at a (page aligned) va
void *return_page_at(uintptr_t address)
{
    if (BITMAP_READ_BIT(kernel_heap_bitmap, (address - kernel_heap_base_address) / PAGE_SIZE) == 0) {
        return 0;
    }
    // remove it from heap bitmap too
    BITMAP_UNSET_BIT(kernel_heap_bitmap, (address - kernel_heap_base_address) / PAGE_SIZE);

    // this also frees which may be dumb but idgaf
    vmm_unmap_single_page(&kernel_pmc, address, 1);

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
    acquire_lock(&malloc_lock);
    return 0;
}

/** This function unlocks what was previously locked by the liballoc_lock
 * function.  If it disabled interrupts, it enables interrupts. If it
 * had acquiried a spinlock, it releases the spinlock. etc.
 *
 * \return 0 if the lock was successfully released.
 */
extern int liballoc_unlock() {
    release_lock(&malloc_lock);
    return 0;
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
                    if (!get_page_at(kernel_heap_base_address + (j * PAGE_SIZE))) {
                        return NULL;
                    }
                }
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
        return_page_at(((uintptr_t)address + (i * PAGE_SIZE)));
    }
    return 1;
}