#include "mm/pmm.h"
#include "std/kprintf.h"
#include "pmm.h"

#include "std/macros.h"

void *kmemset(void *s, uint8_t c, size_t n)
{
    uint8_t *p = (uint8_t *)s;

    for (size_t i = 0; i < n; i++)
    {
        p[i] = c;
    }

    return s;
}

struct limine_memmap_response *memmap = 0x0;
struct limine_hhdm_response *hhdm = 0x0;

// keep track of free and used pages: 0 = unused
uint8_t *page_bitmap = (uint8_t*)0x0;
uint64_t pages_usable = 0x0;
uint64_t pages_reserved = 0x0;
uint64_t pages_total = 0x0;
uint64_t pages_in_use = 0x0;

uint64_t highest_address = 0x0;
uint64_t bitmap_size_bytes = 0x0;

// memmap and hhdm
struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static void initBitmap(void)
{
    uint8_t success = 0;
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size_bytes)
        {
            // put bitmap at the start of fitting entry,
            // need to apply hhdm offset here, because base is physical
            // memory but page_bitmap in kernel needs to be virtual
            page_bitmap = (uint8_t*)(entry->base + hhdm->offset);
            printf("pages for bitmap: %lu", bitmap_size_bytes / PAGE_SIZE);
            kmemset(page_bitmap, 0xFF, bitmap_size_bytes);

            // wipe bitmap space from memmap entry (page aligned size)
            entry->length -= bitmap_size_bytes;
            entry->base += bitmap_size_bytes;

            success = 1;
            break;
        }
    }
    if (!success) {
        // [DBG]
        printf("PMM::NO_SPACE_FOR_PAGE_BITMAP Couldn't fit the page map anwhere\n");
        asm volatile ("cli\n hlt");
    }
}

static void fillBitmap(void)
{
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        // [DBG]
        //printf("Entry %-2lu: Base = 0x%016lx, Length = %lu bytes, Type = %lu\n\r",
        //    i, entry->base, entry->length, entry->type);

        if (entry->type == LIMINE_MEMMAP_USABLE) {

            // free usable entries in the page map
            for (size_t j = 0; j < entry->length / PAGE_SIZE; j++) {
                BITMAP_UNSET_BIT(page_bitmap, (entry->base / PAGE_SIZE) + j);
            }
        }
    }
}

void initPMM(void)
{
    memmap = memmap_request.response;
    hhdm = hhdm_request.response;

    if (!memmap || !hhdm || memmap->entry_count <= 1) {
        printf("PMM::LIMINE_RESPONSE_FAILED memmap or HHDM request failed\n");
        asm volatile ("cli\n hlt");
    }

    // calc page_bitmap size
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            pages_usable += DIV_ROUNDUP(entry->length, PAGE_SIZE);
            highest_address = MAX(highest_address, entry->base + entry->length);
        } else {
            pages_reserved += DIV_ROUNDUP(entry->length, PAGE_SIZE);
        }
    }

    pages_total = highest_address / PAGE_SIZE;
    bitmap_size_bytes = ALIGN_UP(pages_total / 8, PAGE_SIZE);

    initBitmap();
    fillBitmap();

    printf("Total memory: %luMiB, of which %luMiB usable\n",
        (pages_total * PAGE_SIZE) / (1024 * 1024),
        (pages_usable * PAGE_SIZE) / (1024 * 1024));
}

// [TODO] optimize this function, for now loop from the beginning
// OFF BY ONE ERROR [FIXME] (fixed)
void *pmmClaimContiguousPages(size_t count)
{
    size_t idx = 0, pages_found = 0;
    // while not at last page (last idx = total - 1)
    while (idx < pages_total) {
        // if free page at idx
        if (BITMAP_READ_BIT(page_bitmap, idx) == 0) {
            pages_found++;
            // found enough contiguous pages
            if (pages_found == count) {
                // alloc them
                for (size_t j = idx - count; j < idx; j++) {
                    BITMAP_SET_BIT(page_bitmap, j + 1ul);
                }
                pages_in_use += count;
                // [DBG]
                printf("Allocated %lu page(s) at (pa) 0x%016lX\n", count, (unsigned long)((idx + 1ul) - count) * PAGE_SIZE);
                return (void *)(((idx + 1ul) - count) * PAGE_SIZE);
            }
        // the region we are at doesn't contain enough pages
        } else {
            pages_found = 0;
        }
        idx++;
    }

    // [DBG]
    printf("PMM::NO_PAGES_FOUND Free pages: %lu; Requested pages: %lu\n", pages_usable - pages_in_use, count);
    return NULL;
}

// ptr needs to be the original pointer memory was allocated from
// This function does NOT set the pointer to 0 !!!
void pmmFreeContiguousPages(void *ptr, size_t count)
{
    size_t starting_page = (uint64_t)ptr / PAGE_SIZE;
    for (size_t i = 0; i < count; i++) {
        BITMAP_UNSET_BIT(page_bitmap, starting_page + i);
    }
    pages_in_use -= count;
    printf("Freed %lu pages at (pa) 0x%016lX\n", count, (uint64_t)ptr);
}