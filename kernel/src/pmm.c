#include "pmm.h"
#include "kprintf.h"
#include "memory.h"
#include "macros.h"
#include "interrupt.h"

// usable entries are page aligned
struct limine_memmap_response *memmap = NULL;
struct limine_hhdm_response *hhdm = NULL;

struct pmm_usable_entry *pmm_usable_entries = NULL;

// keep track of free and used pages: 0 = unused
uint8_t *pmm_page_bitmap = NULL;
uint64_t pmm_pages_usable = 0;
uint64_t pmm_pages_reserved = 0;
uint64_t pmm_pages_total = 0;
uint64_t pmm_pages_in_use = 0;

uint64_t pmm_highest_address_memmap = 0;
uint64_t pmm_memmap_usable_entry_count = 0;
uint64_t pmm_total_bytes_pmm_structures = 0;
uint64_t pmm_highest_address_usable = 0;
uint64_t pmm_bitmap_size_bytes = 0;

static size_t _allocator_last_index = 0;

// memmap and hhdm
struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static void init_bitmap(void)
{
    // upwards page aligned
    pmm_total_bytes_pmm_structures = ALIGN_UP(pmm_bitmap_size_bytes
        + pmm_memmap_usable_entry_count * sizeof(struct pmm_usable_entry), PAGE_SIZE);
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= pmm_total_bytes_pmm_structures)
        {
            // put bitmap at the start of fitting entry
            pmm_page_bitmap = (uint8_t*)(entry->base + hhdm->offset);
            memset(pmm_page_bitmap, 0xFF, pmm_bitmap_size_bytes);

            // put usable_entry_array right after it
            pmm_usable_entries = (struct pmm_usable_entry *)(entry->base + hhdm->offset + pmm_bitmap_size_bytes);

            // wipe bitmap space from memmap entry (page aligned size)
            entry->length -= ALIGN_UP(pmm_total_bytes_pmm_structures, PAGE_SIZE);
            entry->base += ALIGN_UP(pmm_total_bytes_pmm_structures, PAGE_SIZE);

            return;
        }
    }
    kpanic(NULL, "PMM couldn't fit the page bitmap anywhere\n");
}

static void fill_bitmap(void)
{
    size_t useable_entry_index = 0;
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        // [DBG]
        //kprintf("Entry %-2lu: Base = 0x%p, Length = %lu bytes, Type = %lu\n\r",
        //    i, entry->base, entry->length, entry->type);

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            // free usable entries in the page map
            for (size_t j = 0; j < entry->length / PAGE_SIZE; j++) {
                BITMAP_UNSET_BIT(pmm_page_bitmap, (entry->base / PAGE_SIZE) + j);
            }

            // fill usable entry tracking array
            *((uint64_t *)pmm_usable_entries + useable_entry_index * 2) = entry->base;
            *((uint64_t *)pmm_usable_entries + useable_entry_index * 2 + 1) = entry->length;

            useable_entry_index++;
        }
    }
}

void init_pmm(void)
{
    memmap = memmap_request.response;
    hhdm = hhdm_request.response;

    if (!memmap || !hhdm || memmap->entry_count <= 1) {
        kpanic(NULL, "PMM::LIMINE_RESPONSE_FAILED memmap or hhdm request failed\n");
    }

    // calc page_bitmap size
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        pmm_highest_address_memmap = MAX(pmm_highest_address_memmap, entry->base + entry->length);

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            pmm_memmap_usable_entry_count++;
            pmm_pages_usable += DIV_ROUNDUP(entry->length, PAGE_SIZE);
            pmm_highest_address_usable = MAX(pmm_highest_address_usable, entry->base + entry->length);
        } else {
            pmm_pages_reserved += DIV_ROUNDUP(entry->length, PAGE_SIZE);
        }
    }

    pmm_pages_total = pmm_highest_address_usable / PAGE_SIZE;
    pmm_bitmap_size_bytes = DIV_ROUNDUP(pmm_pages_total, 8);

    init_bitmap();
    fill_bitmap();

    kprintf("  - pmm: total memory: %luMiB, of which %luMiB usable\n",
        (pmm_pages_total * PAGE_SIZE) / (1024 * 1024),
        ((pmm_pages_usable) * PAGE_SIZE) / (1024 * 1024));

    // for no real reason other than why not don't allow running the OS with
    // less than 1 GiB of usable memory
    if ((pmm_pages_usable * PAGE_SIZE) / (1024 * 1024) < 1000) {
        kprintf("stop being cheap and run this with >= 1 GiB of ram\n\r");
    }
}

static void *search_free(size_t *idx, size_t *pages_found, size_t count)
{
    // if free page at idx
    if (BITMAP_READ_BIT(pmm_page_bitmap, *idx) == 0) {
        (*pages_found)++;
        // found enough contiguous pages
        if (*pages_found == count) {
            // alloc them
            for (size_t j = (*idx + 1) - count; j <= *idx; j++) {
                BITMAP_SET_BIT(pmm_page_bitmap, j);
            }
            _allocator_last_index = *idx;
            pmm_pages_in_use += count;
            // [DBG]
            //printf("Allocated %lu page(s) at (pa) 0x%016lX\n", count, (unsigned long)((idx + 1ul) - count) * PAGE_SIZE);
            return (void *)(((*idx + 1ul) - count) * PAGE_SIZE);
        }
    // the region we are at doesn't contain enough pages
    } else {
        *pages_found = 0;
    }
    (*idx)++;
    return NULL;
}

void *pmm_claim_contiguous_pages(size_t count)
{
    size_t idx = _allocator_last_index, pages_found = 0;
    uint8_t can_retry = 1;

retry:
    for (size_t entry_idx = 0; entry_idx < pmm_memmap_usable_entry_count; entry_idx++) {
        // while not at last page (last idx = total - 1)
        idx = MAX(idx, pmm_usable_entries[entry_idx].base / PAGE_SIZE);
        size_t end = (pmm_usable_entries[entry_idx].base + pmm_usable_entries[entry_idx].length) / PAGE_SIZE;
        while (idx < end) {
            void *ptr = search_free(&idx, &pages_found, count);
            if (ptr != NULL)
                return ptr;
        }
    }
    // chance to look again from the beginning
    if (can_retry) {
        _allocator_last_index = 0;
        can_retry = 0;
        idx = 0;
        pages_found = 0;
        goto retry;
    }

    // [DBG]
    kprintf("PMM::NO_PAGES_FOUND Free pages: %lu; Requested pages: %lu\n", pmm_pages_usable - pmm_pages_in_use, count);
    return NULL;
}

// ptr needs to be the original pointer memory was allocated from
void pmm_free_contiguous_pages(void *ptr, size_t count)
{
    size_t starting_page = (uint64_t)ptr / PAGE_SIZE;
    for (size_t i = 0; i < count; i++) {
        BITMAP_UNSET_BIT(pmm_page_bitmap, starting_page + i);
    }
    pmm_pages_in_use -= count;
    // [DBG]
    //printf("Freed %lu pages at (pa) 0x%016lX\n", count, (uint64_t)ptr);
}