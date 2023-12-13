#include "mm/pmm.h"
#include "std/kprintf.h"

void *kmemset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

// keep track of free and used pages: 0 = unused
uint8_t *page_bitmap;
uint64_t highest_address = 0x0;
uint64_t lowest_address = UINT64_MAX;

size_t total_free_bytes = 0;
size_t total_available_pages = 0;
size_t bitmap_size_bytes;

struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static void allocateBitmap(void)
{
    // find free space in the page map to put bitmap at
    size_t pages_needed = (bitmap_size_bytes / 4096) + 1;
    if (total_available_pages < pages_needed) {
        printf("Something went wrong while allocating space for bitmap, aborting...");
        asm volatile("cli\n hlt");
    }
    // just put the bitmap at the first available addresses for now
    page_bitmap = (uint8_t*)(lowest_address + hhdm_request.response->offset);

    printf("Pages needed: %lu\n", pages_needed);
    kmemset((void*)page_bitmap, 0, bitmap_size_bytes);
    // mark pages used by the memory map as 1's
    kmemset((void*)page_bitmap, 1, pages_needed);
    printf("Allocated space for bitmap!\n");
}

void initPMM(void)
{
    if (memmap_request.response == NULL
     || memmap_request.response->entry_count <= 1) {
        printf("memmap request failed, halting...\n");
        asm volatile("cli\n hlt");
    }
    if (hhdm_request.response == NULL) {
        printf("hhdm request failed, halting...\n");
        asm volatile("cli\n hlt");
    }

    // count total available free bytes
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap_request.response->entries[i];
        printf("Entry %-2lu: Base = 0x%016lx, Length = %lu bytes, Type = %lu\n\r",
            i, entry->base, entry->length, entry->type);
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_free_bytes += entry->length;

            if (entry->base < lowest_address)
                lowest_address = entry->base;
            if (entry->base > highest_address)
                highest_address = entry->base + entry->length;

            // should "round down"
            total_available_pages += entry->length / PAGE_SIZE;
        }
    }

    bitmap_size_bytes = total_available_pages;

    printf("Allocating bitmap...\n");
    allocateBitmap();

    printf("Total memory: %lumb usable\n\r", total_free_bytes / (1024 * 1024));
}

void *claimContinousPages(uint64_t count)
{
    // i tracks the page
    for (size_t i = 0; i < total_available_pages; i++) {
        // if we are to far back to find any more free memory
        if (i + count > total_available_pages) {
            return NULL;
        }

        for (size_t j = 0; j < count; j++) {
            if (page_bitmap[i + j] == 1) {
                i += j;
                goto inc;
            }
        }
        return (void*)(lowest_address + hhdm_request.response->offset + i * PAGE_SIZE);

inc:
    }
    return NULL;
}