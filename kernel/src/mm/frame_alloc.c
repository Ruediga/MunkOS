/*
 * This is the physical memory manager. Depending on configuration, either
 * a buddy or bitmap allocator is used for allocating contiguous physical pages.
 * The configuration can be set by defining MUNKOS_CONFIG_BUDDY or MUNKOS_CONFIG_BITMAP.
 * It is highly recommended to stay with the default buddy configuration.
 * Some problems with this are the assumptions that memory holes arent too big,
 * because this would waste some RAM, and that the system doesn't have a high
 * NUMA ratio. I'll keep a NUMA domain based allocator in mind tho, maybe in a few more
 * lifespans I'll implement one. The exposed functions are:
 * allocator_init(), which initializes the necessary structures for the chosen allocator.
 * page_alloc(order), which returns a blob of physical memory of size order².
 * page_free(page, order), which frees the allocated block again.
 * phys_stat_memory(&stat), returns a structure with allocator data
*/

#include "frame_alloc.h"
#include "kprintf.h"
#include "memory.h"
#include "macros.h"
#include "interrupt.h"

struct page *pages;
size_t pages_count;

// in pages
static size_t frame_allocator_total;
static size_t frame_allocator_usable;
static size_t frame_allocator_free;

#ifdef MUNKOS_CONFIG_BITMAP
static void bitmap_init(void);
static void *_bitmap_search_free(size_t *idx, size_t *pages_found, size_t count);
static void *bitmap_page_alloc(size_t count);
static void bitmap_page_free(void *ptr, size_t count);

static k_spinlock_t bitmap_lock;
static uint8_t *bitmap;
static size_t bitmap_last_index = 0;
#endif // MUNKOS_CONFIG_BITMAP

#ifdef MUNKOS_CONFIG_BUDDY
//#define BUDDY_DEBUG_PRINT
#ifdef BUDDY_DEBUG_PRINT
#define DBGPRNT(printf) printf
#else
#define DBGPRNT(printf)
#endif

// per-order structure
struct buddy_zone {
    uint8_t *bitmap;        // each buddy pair has a state-bit
    struct page *list;      // freelist for all free pages in an order
};

struct buddy_context {
    k_spinlock_t this_lock;
    size_t region_start;

    // debugging
    size_t allocation_count,
           deallocation_count;
    struct buddy_zone orders[BUDDY_HIGH_ORDER + 1];
};

struct buddy_context buddy_allocator;

void buddy_init(void);
void buddy_print(void);
struct page *buddy_alloc(size_t order);
void buddy_free(struct page *page, size_t order);

#endif // MUNKOS_CONFIG_BUDDY

inline void allocator_init()
{
    pages_count = frame_allocator_total = early_mem_init();
    early_mem_statistics(&frame_allocator_usable, &frame_allocator_free);

    // this part is responsible for exiting early mem phase
    // (BEFORE allocating any memory themselves)
#ifdef MUNKOS_CONFIG_BITMAP
    bitmap_init();
#endif
#ifdef MUNKOS_CONFIG_BUDDY
    buddy_init();
#endif
}

inline struct page *page_alloc(size_t order)
{
#ifdef MUNKOS_CONFIG_BITMAP
    // doesn't work properly yet since this function doesnt return struct page
    //return _bitmap_page_alloc(order2size(order));
    //MISSING
    (void)order;
    return NULL;
#endif
#ifdef MUNKOS_CONFIG_BUDDY
    return buddy_alloc(order);
#endif
}

// for now use this temp function
void *page_alloc_temp(size_t order)
{
#ifdef MUNKOS_CONFIG_BITMAP
    return (void *)(bitmap_page_alloc(order2size(order)));
#endif
#ifdef MUNKOS_CONFIG_BUDDY
    struct page *pg = buddy_alloc(order);
    return (void *)(page2idx(NNULL(pg)) * PAGE_SIZE);
#endif
}

inline void page_free(struct page *page, size_t order)
{
#ifdef MUNKOS_CONFIG_BITMAP
    bitmap_page_free((void *)(page2idx(page) * PAGE_SIZE), order2size(order));
#endif
#ifdef MUNKOS_CONFIG_BUDDY
    buddy_free(page, order);
#endif
}

void page_free_temp(void *address, size_t size)
{
    page_free(phys2page((uintptr_t)address), psize2order(size));
}

void phys_stat_memory(struct phys_mem_stat *stat)
{
#ifdef MUNKOS_CONFIG_BITMAP
    acquire_lock(&bitmap_lock);
#endif
#ifdef MUNKOS_CONFIG_BUDDY
    acquire_lock(&buddy_allocator.this_lock);
#endif

    stat->free_pages = frame_allocator_free;
    stat->total_pages = frame_allocator_total;
    stat->usable_pages = frame_allocator_usable;

#ifdef MUNKOS_CONFIG_BITMAP
    release_lock(&bitmap_lock);
#endif
#ifdef MUNKOS_CONFIG_BUDDY
    release_lock(&buddy_allocator.this_lock);
#endif
}

// ============================================================================
// BITMAP
// ============================================================================

#ifdef MUNKOS_CONFIG_BITMAP
static void bitmap_init(void)
{
    size_t bitmap_size_bytes = DIV_ROUNDUP(frame_allocator_total, 8);

    bitmap = early_mem_alloc(bitmap_size_bytes);

    for (size_t i = 0; i < memmap_entry_count; i++) {
        struct memmap_entry *entry = &memmap[i];

        // [DBG]
        //kprintf("Entry %-2lu: Base = 0x%p, Length = %lu bytes, Type = %lu\n\r",
        //    i, entry->base, entry->length, entry->type);

        if (entry->type ==LIMINE_MEMMAP_USABLE) {
            // free usable entries in the page map
            for (size_t j = 0; j < entry->length / PAGE_SIZE; j++) {
                BITMAP_UNSET_BIT(bitmap, (entry->start / PAGE_SIZE) + j);
            }
        }
    }

    kprintf("  - pmm: total memory: %luMiB, of which %luMiB usable\n",
        MiB(frame_allocator_total * PAGE_SIZE), MiB(frame_allocator_usable * PAGE_SIZE));

    // for no real reason other than why not don't allow running the OS with
    // less than 64 MiB of usable memory
    if (MiB(frame_allocator_usable * PAGE_SIZE) < 64) {
        kprintf("stop being cheap and run this with >= 64 MiB of ram\n\r");
    }

    early_mem_exit();
}

static void *_bitmap_search_free(size_t *idx, size_t *pages_found, size_t count)
{
    // if free page at idx
    if (BITMAP_READ_BIT(bitmap, *idx) == 0) {
        (*pages_found)++;
        // found enough contiguous pages
        if (*pages_found == count) {
            // alloc them
            for (size_t j = (*idx + 1) - count; j <= *idx; j++) {
                BITMAP_SET_BIT(bitmap, j);
            }
            bitmap_last_index = *idx;
            frame_allocator_free -= count;
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

void *bitmap_page_alloc(size_t count)
{
    acquire_lock(&bitmap_lock);
    size_t idx = bitmap_last_index, pages_found = 0;
    uint8_t can_retry = 1;

retry:
    for (size_t entry_idx = 0; entry_idx < memmap_entry_count; entry_idx++) {
        // extra array wouldn't hurt but since buddy is preferred anyways idc
        if (memmap[entry_idx].type != LIMINE_MEMMAP_USABLE)
            continue;

        // while not at last page (last idx = total - 1)
        idx = MAX(idx, memmap[entry_idx].start / PAGE_SIZE);
        size_t end = (memmap[entry_idx].start + memmap[entry_idx].length) / PAGE_SIZE;
        while (idx < end) {
            void *ptr = _bitmap_search_free(&idx, &pages_found, count);
            if (ptr != NULL) {
                release_lock(&bitmap_lock);
                return ptr;
            }
        }
    }
    // chance to look again from the beginning
    if (can_retry) {
        bitmap_last_index = 0;
        can_retry = 0;
        idx = 0;
        pages_found = 0;
        goto retry;
    }

    // [DBG]
    kprintf("PMM::NO_PAGES_FOUND Free pages: %lu; Requested pages: %lu\n",
        frame_allocator_free, count);

    release_lock(&bitmap_lock);
    return NULL;
}

// ptr needs to be the original pointer memory was allocated from
void bitmap_page_free(void *ptr, size_t count)
{
    acquire_lock(&bitmap_lock);
    size_t starting_page = (uint64_t)ptr / PAGE_SIZE;
    for (size_t i = 0; i < count; i++) {
        BITMAP_UNSET_BIT(bitmap, starting_page + i);
    }
    frame_allocator_free += count;
    // [DBG]
    //printf("Freed %lu pages at (pa) 0x%016lX\n", count, (uint64_t)ptr);
    release_lock(&bitmap_lock);
}
#endif // MUNKOS_CONFIG_BITMAP

// ============================================================================
// BUDDY
// ============================================================================

#ifdef MUNKOS_CONFIG_BUDDY
// lru insertion of new into dll at head
static inline void _list_link(struct page *new, struct page **head) {
    new->prev = NULL;
    new->next = *head;
    if (*head)
        (*head)->prev = new;
    *head = new;
}

// unlink entry from dll
static inline void _list_unlink(struct page *entry, struct page **head) {
    if (!*head) kpanic(0, NULL, "trying to unlink list entry from empty head (%s:%d)\n", __FILE__, __LINE__);

    if (*head == entry) {
        *head = entry->next;
        // unnecessary
        //(*head)->prev = NULL;
    }

    if (entry->prev)
        entry->prev->next = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;

    entry->prev = (struct page *)0xDEADBEEF;
    entry->next = (struct page *)0xDEADBEEF;
}

// remove first entry from list and return it
static inline struct page *_list_get(struct page **head) {
    if (!*head) return NULL;

    struct page *ret = *head;
    *head = ret->next;
    if (*head)
        (*head)->prev = NULL;

    ret->next = (struct page *)0xDEADBEEF;
    ret->prev = (struct page *)0xDEADBEEF;

    return ret;
}

// flip bit n
static inline void _bitmap_flip(size_t n, uint8_t *bitmap) {
    size_t byte_idx = n >> 3;               // index / 8
    size_t bit_idx = n & 0b111;             // index % 8
    *(bitmap + byte_idx) ^= (1 << bit_idx); // flip bit
}

// return bit at n
static inline uint8_t _bitmap_status(size_t n, uint8_t *bitmap) {
    size_t byte_idx = n >> 3;               // index / 8
    size_t bit_idx = n & 0b111;             // index % 8
    return (*(bitmap + byte_idx) & (1 << bit_idx)) >> bit_idx;
}

// does all the necessary calculations to flip
// the correct bit in the correct bitmap for page n with order
static inline void order_flip_at(size_t n, size_t order) {
    _bitmap_flip(n >> (order + 1), buddy_allocator.orders[order].bitmap);
}

// does all the necessary calculations to return
// the corresponding bit for page n with order
static inline uint8_t order_status_at(size_t n, size_t order) {
    return _bitmap_status(n >> (order + 1), buddy_allocator.orders[order].bitmap);
}

static inline size_t get_alignment_order(uintptr_t address) {
    size_t order = 0;
    address /= 4096;
    while (!(address & 1) && (order < BUDDY_HIGH_ORDER)) {
        address >>= 1;
        order++;
    }
    return order;
}

static inline struct page *get_buddy(struct page *page, size_t order) {
    uintptr_t address = page2idx(page);
    return phys2page((address ^ order2size(order)) * PAGE_SIZE);
}

// return the biggest pow(2) blocks order where order <= BUDDY_HIGH_ORDER that fits between low and high 
// and the block is aligned to low
static inline size_t _slice_range(uintptr_t low, uintptr_t high) {
    high = MIN(high, low + (order2size(get_alignment_order(low)) * PAGE_SIZE));
    uintptr_t size = (high - low) / PAGE_SIZE;
    size_t order = 0;

    while ((size >>= 1) && (order < BUDDY_HIGH_ORDER)) {
        order++;
    }
    
    return order;
}

void buddy_print(void) {
    size_t free = frame_allocator_free, usable = frame_allocator_usable;
    kprintf("buddy allocator: %lu pages out of %lu free (~%lu.%02lu%%)\n", 
        free, usable, (free * 100) / usable, ((free * 10000) / usable) % 100);
    kprintf("buddy allocator: allocations: %lu, deallocations: %lu\n",
        buddy_allocator.allocation_count, buddy_allocator.deallocation_count);

    for (size_t i = 0; i <= BUDDY_HIGH_ORDER; i++) {
        kprintf("freelist: order %lu (%lu): ", i, order2size(i));
        struct page *curr = buddy_allocator.orders[i].list;

        size_t limit = 0;
        while (curr) {
            if (limit++ > 10) {
                kprintf("...");
                break;
            }

            if (i == BUDDY_HIGH_ORDER)
                kprintf("%lu ", page2idx(curr));
            else
                kprintf("%lu(%lu) ", page2idx(curr), (uint64_t)order_status_at(page2idx(curr), i));
            curr = curr->next;
        }
        kprintf("\n");
    }
}

void buddy_init()
{
    // find space for bitmap
    for (size_t order = 0; order <= BUDDY_HIGH_ORDER; order++) {
        struct buddy_zone *zone = &buddy_allocator.orders[order];

        size_t bitmap_order_size = (frame_allocator_total / order2size(order) / sizeof(uint8_t)) + 1;
        zone->bitmap = early_mem_alloc(bitmap_order_size);
    }

    early_mem_statistics(&frame_allocator_usable, &frame_allocator_free);
    size_t allocated = early_mem_exit();
    // dont forget to update stats
    early_mem_statistics(&frame_allocator_usable, &frame_allocator_free);
    early_mem_dbg_print();
    kprintf("lost %lu pages (~%lu.%lu%%)\n", allocated / PAGE_SIZE, ((allocated / PAGE_SIZE) * 100)
        / frame_allocator_total, (allocated / frame_allocator_total) % 100);
    
    DBGPRNT(kprintf("Initializing buddy allocator with %lu pages\n", frame_allocator_total));
    
    // find out which pages are actually free to use. Mark the entries in the bitmap,
    // and split the memory up in as big blocks as possible. Add these to the freelists.
    for (size_t i = 0; i < memmap_entry_count; i++) {
        struct memmap_entry *ent = &memmap[i];
        if (ent->type != LIMINE_MEMMAP_USABLE) continue;

        uintptr_t current = ent->start;
        uintptr_t end = ent->start + ent->length;
        while (current < end) {
            size_t buddy = _slice_range(current, end);
            struct page *start = phys2page(current);
            _list_link(start, &buddy_allocator.orders[buddy].list);

            DBGPRNT(kprintf("i %lu (size %lu) freelist entry (order %lu) (buddy %lu)\n",
                   page2idx(start), order2size(buddy), buddy, page2idx(get_buddy(start, buddy))));
            current += order2size(buddy) * PAGE_SIZE;
        }
    }

    for (size_t i = 0; i <= BUDDY_HIGH_ORDER; i++) {
        struct page *curr = buddy_allocator.orders[i].list;
        while (curr) {
            size_t this_idx = page2idx(curr);
            order_flip_at(this_idx, i);

            curr = curr->next;
        }
    }

    //buddy_print();
}

// allocate 2 ^ order pages. these are not guaranteed to be linked,
// but will be physically contiguous. performs sanity checks.
struct page *buddy_alloc(size_t order) {
    size_t cpy_order = order;

    if (order > BUDDY_HIGH_ORDER) {
        kprintf("buddy allocations of order > %lu are not supported!\n", BUDDY_HIGH_ORDER);
        buddy_print();
        return NULL;
    }

    // no need to lock before the check
    acquire_lock(&buddy_allocator.this_lock);

    struct page *pg;

    // find first either splittable or returnable buddy
    while (order <= BUDDY_HIGH_ORDER) {
        pg = _list_get(&buddy_allocator.orders[order].list);
        if (pg) goto found_buddy;
        // try splitting at next order
        order++;
    }
    // no order to split from found
    kprintf("no page to split from\n");
    release_lock(&buddy_allocator.this_lock);
    return NULL;

found_buddy:
    DBGPRNT(kprintf("found first splittable/returnable entry at order %lu (page = %lu)\n", order, page2idx(pg)));

    // split buddy: add second buddy to free list, flip its bit
    order_flip_at(page2idx(pg), order);
    DBGPRNT(kprintf("flipped order %lu block at %lu (status = %u)\n", order, page2idx(pg),
        order_status_at(page2idx(pg), order)));

    while (order > cpy_order) {
        order--;

        DBGPRNT(kprintf("splitting original: %lu, buddy = %lu\n", page2idx(pg), page2idx(get_buddy(pg, order))));

        struct page *buddy = get_buddy(pg, order);
        if (order_status_at(page2idx(buddy), order)) {
            kpanic(0, NULL, "trying to split into invalid buddy entry\n");
        }

        // add split entry to free list
        _list_link(buddy, &buddy_allocator.orders[order].list);

        // flip split buddy pair
        order_flip_at(page2idx(pg), order);
    }

    frame_allocator_free -= order2size(cpy_order);
    buddy_allocator.allocation_count++;

    DBGPRNT(kprintf("returning page %lu\n", page2idx(pg)));
    release_lock(&buddy_allocator.this_lock);
    return pg;
}

// free 2 ^ n pages. performs sanity checks.
void buddy_free(struct page *page, size_t order) {
    acquire_lock(&buddy_allocator.this_lock);

    size_t cpy_order = order;

    // firstly, perform some sanity checks
    if (page2idx(page) % order2size(order)) {
        kpanic(0, NULL, "trying to free page %lu of order %lu, when alignment order is %lu\n",
            page2idx(page), order, get_alignment_order(page2idx(page) * PAGE_SIZE));
    }
    DBGPRNT(kprintf("attempting to free page %lu, order %lu\n", page2idx(page), order));

    // try to merge up to highest possible order: while bitmap entry for this buddy == 0,
    // delete buddy from it's free list, and go up one order.
    // finally, add merged entry back into freelist.

    while (order < BUDDY_HIGH_ORDER) {
        order_flip_at(page2idx(page), order);

        if (order_status_at(page2idx(page), order)) {
            DBGPRNT(kprintf("status: page %lu not ready to merge with %lu\n",
                page2idx(page), page2idx(get_buddy(page, order))));

            // not ready to merge
            break;
        }

        // unlink the buddy to merge with, and flip it's bit (1 to zero)
        struct page *buddy = get_buddy(page, order);
        DBGPRNT(kprintf("trying to unlink buddy %lu\n", page2idx(buddy)));
        _list_unlink(buddy, &buddy_allocator.orders[order].list);
        DBGPRNT(kprintf("status: page %lu merged with %lu\n",
            page2idx(page), page2idx(get_buddy(page, order))));

        // the buddies have effectively been merged, try to jump up one order,
        // which requires page to hold the struct for the first one of the two buddies
        page = (struct page *)MIN((uintptr_t)page, (uintptr_t)buddy);

        order++;
    }

    // page holds the pointer to the merged buddy, add it back to it's free list
    _list_link(page, &buddy_allocator.orders[order].list);

    frame_allocator_free += order2size(cpy_order);
    buddy_allocator.deallocation_count++;

    DBGPRNT(kprintf("page %lu, order %lu freed\n", page2idx(page), order));
    release_lock(&buddy_allocator.this_lock);
}
#endif // MUNKOS_CONFIG_BUDDY