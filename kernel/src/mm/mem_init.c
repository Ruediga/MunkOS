/*
 * Early memory allocator. First fit allocates memory directly from the memmap
 * via a first fit algorithm. Functions provided:
 * early_mem_init(memmap, memmap_entries), sets up basic structures for the allocator
 * early_mem_alloc(size), allocates size contiguous bytes
 * early_mem_exit(), cleans up memmap copy, from now on unusable
 * early_mem_statistics(&usable, &free), stores total usable memory and free memory in pages
 * early_mem_dbg_print(), self explaining
*/

#include "frame_alloc.h"
#include "interrupt.h"
#include "macros.h"
#include "kprintf.h"
#include "memory.h"
#include "compiler.h"

// for each allocation keep a record so we can map memory
// allocated through it into our page tables
#define early_mem_max_allocations (PAGE_SIZE / sizeof(struct early_mem_alloc_mapping))
struct early_mem_alloc_mapping early_mem_mappings[early_mem_max_allocations];
size_t early_mem_allocations;

static struct limine_memmap_response *limine_memmap;
struct limine_hhdm_response *hhdm;
struct memmap_entry *memmap;
size_t memmap_entry_count;

static size_t early_mem_total_pages;
static size_t early_mem_total_pages_usable;
static size_t early_mem_bytes_allocated;

static int early_mem_is_initialized = 0;

struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

size_t early_mem_init()
{
    if (!memmap_request.response || !hhdm_request.response) {
        kpanic(0, NULL, "memmap or hhdm request failed\n");
    }

    hhdm = hhdm_request.response;
    limine_memmap = memmap_request.response;
    memmap_entry_count = limine_memmap->entry_count;

    // find place to put memmap cpy at
    size_t space = memmap_entry_count * sizeof(struct memmap_entry);
    for (size_t i = 0; i < memmap_entry_count; i++) {
        struct limine_memmap_entry *ent = limine_memmap->entries[i];

        if ((ent->type == LIMINE_MEMMAP_USABLE) && (ent->length >= space)) {
            memmap = (struct memmap_entry *)(ent->base + hhdm->offset);
            early_mem_bytes_allocated += space;
            break;
        }
    }

    // copy limines memmap
    size_t high = 0, usable_bytes = 0;
    for (size_t i = 0; i < memmap_entry_count; i++) {
        struct limine_memmap_entry *ent = limine_memmap->entries[i];

        if (ent->type == LIMINE_MEMMAP_USABLE) usable_bytes += ent->length;

        high = MAX(high, ent->base + ent->length);

        memmap[i].start = ent->base;
        memmap[i].length = ent->length;
        memmap[i].type = ent->type;

        if ((uintptr_t)memmap == ent->base + hhdm->offset) {
            // cut out memmap copy
            memmap[i].start += space;
            memmap[i].length -= space;
        }
    }
    early_mem_total_pages = high / PAGE_SIZE;
    early_mem_total_pages_usable = usable_bytes / PAGE_SIZE;

    early_mem_is_initialized = 1;

    pages = early_mem_alloc(early_mem_total_pages * sizeof(struct page));
    memset(pages, 0x00, early_mem_total_pages * sizeof(struct page));

    return early_mem_total_pages;
}

// very early first fit algorithm, memory returned is hhdm-ed
// mustn't fail
void *early_mem_alloc(size_t size)
{
    if (!early_mem_is_initialized) {
        kpanic(0, NULL, "trying to call early_mem_alloc() with early_mem_uninitialized=0\n");
    }

    if (early_mem_allocations > early_mem_max_allocations) {
        // we have a limited amount of allocations because of vmm mapping tracking
        kpanic(0, NULL, "tried to exceeded max allocation amount for early_mem\n");
    }

    for (size_t i = 0; i < memmap_entry_count; i++) {
        struct memmap_entry *ent = &memmap[i];

        if ((ent->type == LIMINE_MEMMAP_USABLE) && (ent->length >= size)) {
            // cut out memmap copy
            void *ret = (void *)(ent->start + hhdm->offset);

            early_mem_mappings[early_mem_allocations].start = ent->start;
            early_mem_mappings[early_mem_allocations].length = size;
            early_mem_allocations++;

            ent->start += size;
            ent->length -= size;
            early_mem_bytes_allocated += size;
            return ret;
        }
    }

    kpanic(KPANIC_FLAGS_DONT_TRACE_STACK | KPANIC_FLAGS_THIS_CORE_ONLY, NULL, "early_mem_alloc(%lu) failed\n", size);
    unreachable();
    return NULL;
}

// cleans up data structures, fixes memmap
size_t early_mem_exit(void)
{
    if (!early_mem_is_initialized) {
        kpanic(0, NULL, "trying to call early_mem_exit() with early_mem_uninitialized=0\n");
    }

    for (size_t i = 0; i < memmap_entry_count; i++) {
        struct memmap_entry *ent = &memmap[i];

        // make sure all memmap entries are still aligned to page size
        if ((ent->type == LIMINE_MEMMAP_USABLE)) {
            size_t cut = ALIGN_UP(ent->start, PAGE_SIZE) - ent->start;
            ent->start += cut;
            ent->length -= cut;
            early_mem_bytes_allocated += cut;
        }
    }

    // sanity check
    if (early_mem_bytes_allocated & (PAGE_SIZE - 1)) {
        kpanic(0, NULL, "early_mem_exit() corrupted memory map\n");
    }

    early_mem_total_pages_usable -= early_mem_bytes_allocated / PAGE_SIZE;
    return early_mem_bytes_allocated;
}

void early_mem_dbg_print(void)
{
    kprintf("0x%lx bytes ( >= %lu pages) allocated until now\n",
        early_mem_bytes_allocated, early_mem_bytes_allocated / PAGE_SIZE);
    for (size_t i = 0; i < memmap_entry_count; i++) {
        kprintf("memmap: region %lu starting at 0x%lx (%lu page(s)): !usable=%lu\n",
            i, memmap[i].start, memmap[i].length / PAGE_SIZE, memmap[i].type);
    }
}

// return pages usable and bytes free, can be called even after early_mem_exit()
void early_mem_statistics(size_t *usable, size_t *free) {
    if (!early_mem_is_initialized) {
        kpanic(0, NULL, "trying to call early_mem_statistics() with early_mem_uninitialized=0\n");
    }
    *usable = early_mem_total_pages_usable;
    *free = early_mem_total_pages_usable - early_mem_bytes_allocated / PAGE_SIZE;
}