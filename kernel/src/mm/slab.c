#include <stddef.h>

#include "frame_alloc.h"
#include "kheap.h"
#include "kprintf.h"
#include "interrupt.h"
#include "memory.h"
#include "vmm.h"
#include "compiler.h"

// Basic slab allocator. Allocations on pow2 sized blocks are guaranteed to be aligned,
// if allocated via the generic cache pools. When the sanitizer is enabled,
// the blocks alignment changes to 64 bytes.
// A driver can create its own pools for objects with specific sizes,
// and then specifically allocate from them (similar to linux kmem_cache).
// Sanity checks and the following sanitizers are (kinda) implemented:
//   - buffer over-/underflow
//   - primitive double free protection
// missing:
//   - use after free

// slab layout?

struct slab_cache;

// reuse struct page for slab object data (40 bytes) (sync this up!)
struct slab {
    int32_t flags;
    struct slab *next;                  // dll of slabs in a cache
    struct slab *prev;
    struct slab_cache *this_cache;      // reference
    void *freelist;    // linked list of free slab objects. link when free'd,
                        // unlink when allocated. exceptions: full, empty
    uint16_t used_objs;
    uint16_t total_objs;
};

struct slab_cache {
    char *name;
    uint16_t unused;                    // for alignment
    uint16_t pages_per_slab;
    uint16_t obj_size;                  // raw size of object
    uint16_t obj_md_size;               // size of object + metadata
    size_t total_objs;
    size_t total_objs_allocated;
    size_t full_slab_count, partial_slab_count, empty_slab_count;
    struct slab *full_slabs;            // dll for full slabs (newly allocated or all freed)
    struct slab *partial_slabs;         // partially full slabs (preffered one to alloc from)
    struct slab *empty_slabs;           // completely empty slabs (need to be free)
    k_spinlock_t lock;
};

size_t slab_initialized;
struct slab_cache kmalloc_generic_caches[KMALLOC_ALLOC_SIZES];
size_t kmalloc_pps_mappings[KMALLOC_ALLOC_SIZES] = {
    // 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
       1,  1,  1,  1,   2,   4,   8,    8,    16,   16
};

// phys starting address of this slab
static inline void *slab2addr(struct slab *slab)
{
    // do some error checking here?
    struct page *pg = (struct page *)slab;

    return (void *)(page2idx(pg) * PAGE_SIZE);
}

static inline size_t _size2block(size_t size)
{
    if (size == 0)
        kpanic(0, NULL, "size == 0");

    // min block size 16 bytes
    size_t order = 0;
    size--;
    size >>= 3;
    while (size >>= 1) {
        order++;
    }

    return order;
}

// stack insertion of new into dll at head
static inline void _list_link(struct slab *new, struct slab **head) {
    new->prev = NULL;
    new->next = *head;
    if (*head)
        (*head)->prev = new;
    *head = new;
}

// unlink entry from dll
static inline void _list_unlink(struct slab *entry, struct slab **head) {
    if (!*head) kpanic(0, NULL, "head == 0");

    if (*head == entry)
        *head = entry->next;

    if (entry->prev)
        entry->prev->next = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;

    entry->prev = (struct slab *)0xDEADBEEF;
    entry->next = (struct slab *)0xDEADBEEF;
}

// marks all pages except for head of a composite page
// as being part of one, so we can find the head of the comp page
static inline void _mark_composite_page(struct page *head, size_t num_pgs)
{
    if (!num_pgs || head->flags & STRUCT_PAGE_FLAG_SLAB_COMPOSITE_HEAD) {
        kpanic(0, NULL, "tried to create composite page where head was part of tail (num_pgs=%lu)\n", num_pgs);
    }

    head->flags |= STRUCT_PAGE_FLAG_SLAB_COMPOSITE_HEAD;

    size_t i = page2idx(head);
    num_pgs--;
    for (struct page *pg = NULL; num_pgs; num_pgs--) {
        pg = &pages[i + num_pgs];

        if (pg->flags & STRUCT_PAGE_FLAG_COMPOSITE_TAIL) {
            kpanic(0, NULL, "page was part of composite tail already\n");
        }

        pg->flags |= STRUCT_PAGE_FLAG_COMPOSITE_TAIL;
        pg->comp_head = head;
    }
}

static inline void _unmark_composite_page(struct page *head, size_t num_pgs)
{
    head->flags &= ~STRUCT_PAGE_FLAG_SLAB_COMPOSITE_HEAD;

    size_t i = page2idx(head);
    num_pgs--;
    for (struct page *pg = NULL; num_pgs; num_pgs--) {
        pg = &pages[i + num_pgs];

        if (!(pg->flags & STRUCT_PAGE_FLAG_COMPOSITE_TAIL)) {
            kpanic(0, NULL, "page wasn't part of composite tail\n");
        }

        pg->flags &= ~STRUCT_PAGE_FLAG_COMPOSITE_TAIL;
    }
}

// initialize a single slab_cache
static void init_slab_cache(struct slab_cache *c, size_t alloc_size, const char *name)
{
    if (c->name) kpanic(0, NULL, "slab_cache already initalized");

    c->empty_slabs = c->full_slabs = c->partial_slabs = NULL;
    c->lock.lock = c->lock.dumb_idea = 0;
    c->name = (char *)name;
    
    // not pow2 or not in range abort
    if (alloc_size & (alloc_size - 1) || alloc_size < KMALLOC_ALLOC_MIN || alloc_size > KMALLOC_ALLOC_MAX)
        kpanic(0, NULL, "not pow2");

    c->obj_size = alloc_size;

    c->total_objs = 0;
    c->total_objs_allocated = 0;
    c->full_slab_count = c->partial_slab_count = c->empty_slab_count = 0;

    // account for kasan metadata
#ifdef CONFIG_SLAB_SANITIZE
    c->obj_md_size = c->obj_size + KMALLOC_REDZONE_LEFT + KMALLOC_REDZONE_RIGHT;
#else
    c->obj_md_size = c->obj_size;
#endif

    // min alloc size 16
    c->pages_per_slab = kmalloc_pps_mappings[size2order(alloc_size >> 5)];
}

void slab_init()
{
    for (int i = 0; i < KMALLOC_ALLOC_SIZES; i++) {
        init_slab_cache(&kmalloc_generic_caches[i], 1 << (i + 4), "some cache");
    }

    for (int i = 0; i < KMALLOC_ALLOC_SIZES; i++) {
        kprintf("slab_cache %d \"%s\" (size %hu) initialized\n", i, kmalloc_generic_caches[i].name, kmalloc_generic_caches[i].obj_size);
    }

    slab_initialized = 1;
}

// allocate a new slab from the buddy allocator for a given slab_cache.
// slab will contain AT LEAST min_objects total objects.
// the more objects per slab, the more potential memory waste
// the less objects per slab, the more buddy allocations
//
// call with c->lock being held
static void new_slab(struct slab_cache *c, size_t page_count)
{
    const size_t alloc_order = size2order(page_count);

    // rounds up
    struct slab *new_slab = (struct slab *)page_alloc(alloc_order);

    new_slab->this_cache = c;
    new_slab->total_objs = (order2size(alloc_order) * PAGE_SIZE) / c->obj_md_size;
    if (new_slab->total_objs != (c->pages_per_slab * PAGE_SIZE) / c->obj_md_size) {
        kpanic(0, NULL, "check failed\n");
    }
    new_slab->used_objs = 0;

    // generate freelist
    // for each object, put a pointer to the next free object
    uint8_t *slab_data = (uint8_t *)((uintptr_t)slab2addr(new_slab) + (uintptr_t)hhdm->offset);

    for (int i = 0; i < new_slab->total_objs - 1; i++) {
        void *next = slab_data + (i + 1) * c->obj_md_size;

        // next block pointer
        *((uintptr_t *)(slab_data + i * c->obj_md_size)) = (uintptr_t)next;
    }
    // last one nullptr
    *((uintptr_t *)(slab_data + (new_slab->total_objs - 1) * c->obj_md_size)) = (uintptr_t)NULL;

    new_slab->freelist = (void *)slab_data;

    _list_link(new_slab, &c->full_slabs);
    c->full_slab_count++;

    c->total_objs += new_slab->total_objs;

    _mark_composite_page((struct page *)new_slab, order2size(alloc_order));
}

// attempt to evict some full slabs from cache.
// call with c->lock being held
static void _attempt_free_full(struct slab_cache *c)
{
    // policy: keep slab order + 1 many slabs in cache
    if (c->full_slab_count > _size2block(c->obj_md_size) + 1) {
        // evict slab
        struct slab *s = c->full_slabs;

        _list_unlink(s, &c->full_slabs);
        c->full_slab_count--;

        c->total_objs -= s->total_objs;

        _unmark_composite_page((struct page *)s, c->pages_per_slab);

        page_free((struct page *)s, size2order(c->pages_per_slab));
    }
}

static void *cache_alloc(struct slab_cache *c)
{
    // lock everything for now
    spin_lock(&c->lock);

    // prefer to allocate from partial slabs. if there aren't any,
    // allocate from full slabs. if there aren't any, allocate new full
    // slabs

    struct slab *part = c->partial_slabs;
    if (part) {
        if (!part->freelist) {
            kpanic(0, NULL, "partial->freelist is empty, but total=%hu, used=%hu, size=%lu\n",
                part->total_objs, part->used_objs, c->obj_size);
        }

        // alloc from partial
        void *ret = part->freelist;

        part->this_cache->total_objs_allocated++;
        if (++part->used_objs == part->total_objs) {
            // if emptied
            part->freelist = (void *)*((uintptr_t *)part->freelist);
            if (part->freelist) {
                kpanic(0, NULL, "part->freelist has items, but total-used=%hu, size=%lu\n",
                    part->total_objs - part->used_objs, c->obj_size);
            }

            _list_unlink(part, &c->partial_slabs);
            c->partial_slab_count--;
            _list_link(part, &c->empty_slabs);
            c->empty_slab_count++;
        } else {
            // advance freelist
            part->freelist = (void *)*((uintptr_t *)part->freelist);
        }

        spin_unlock(&c->lock);
        return ret;
    }

    if (part) kpanic(0, NULL, "slab error");

got_full:
    struct slab *full = c->full_slabs;
    if (full) {
        // we have full slabs, alloc from one

        _list_unlink(full, &c->full_slabs);
        c->full_slab_count--;
        _list_link(full, &c->partial_slabs);
        c->partial_slab_count++;

        void *ret = full->freelist;

        // advance freelist
        full->freelist = (void *)*((uintptr_t *)full->freelist);

        full->used_objs++;
        full->this_cache->total_objs_allocated++;

        spin_unlock(&c->lock);
        return ret;
    }

    // we don't have any full slabs
    new_slab(c, c->pages_per_slab);
    goto got_full;

    unreachable();
}

static struct slab *_find_corresponding_slab(void *addr)
{
    // find struct page corresponding to this address
    struct page *this = phys2page((uintptr_t)addr - (uintptr_t)hhdm->offset);

    // trace back composite page
    if (this->flags & STRUCT_PAGE_FLAG_SLAB_COMPOSITE_HEAD) {
        return (struct slab *)this;
    } else if (this->flags & STRUCT_PAGE_FLAG_COMPOSITE_TAIL) {
        return (struct slab *)this->comp_head;
    } else {
        kpanic(0, NULL, "Invalid slab flags for %p\n", addr);
        unreachable();
    }
}

// allocate from the kmalloc_generic_caches. alloc sizes are rounded up to powers
// of 2. pow2 allocations are aligned.
// since I noticed that I have a tendency to use unitialized malloc'ed memory,
// I should invalidate all memory I return so stuff doesnt randomly break on real
// hw where I dont get conviniently zeroed out memory
void *kmalloc(size_t size)
{
    if (size > KMALLOC_MAX_CACHE_SIZE) {
        // check double free here too
        struct page *ret = page_alloc(psize2order(size));
        ret->flags |= STRUCT_PAGE_FLAG_KMALLOC_BUDDY;
        ret->order = psize2order(size);
        return (void *)(hhdm->offset + page2idx(ret) * PAGE_SIZE);
    }

    if (size == 0) {
        return NULL;
    }

    // get slab
    if (!slab_initialized)
        kpanic(0, NULL, "slab isn't initialized");

    size_t i = _size2block(size);
    if (i >= KMALLOC_ALLOC_SIZES) {
        kpanic(0, NULL, "kmalloc: tried to allocate %lu (max %d)\n", size, KMALLOC_MAX_CACHE_SIZE);
    }
    struct slab_cache *cache = &kmalloc_generic_caches[i];

    // alloc from this cache
    void *ret = cache_alloc(cache);

#ifdef CONFIG_SLAB_SANITIZE
    ret += KMALLOC_REDZONE_LEFT;

    uint64_t *tmp = ret;
    // make sure to reset 
    *tmp = 0;

    uint8_t *ptr = ret - KMALLOC_REDZONE_LEFT;
    for (int i = 0; i < KMALLOC_REDZONE_LEFT; i++) {
        *ptr = KMALLOC_SANITIZE_BYTE;
        ptr++;
    }

    // from obj end to cache obj size end && obj end - sizeof(size_t) sanitize bytes,
    // then the size itself at the absolute end
    ptr = ret + size;
    for (int i = 0; i < KMALLOC_REDZONE_RIGHT + ((int)cache->obj_size - (int)size) - (int)sizeof(size_t); i++) {
        *ptr = KMALLOC_SANITIZE_BYTE;
        ptr++;
    }
    *((size_t *)ptr) = size;
    if (ptr != (ret - KMALLOC_REDZONE_LEFT + cache->obj_md_size - sizeof(size_t)))
        kpanic(0, NULL, "pointer check failed %p->%p\n",
            ptr, (ret - KMALLOC_REDZONE_LEFT + cache->obj_md_size - sizeof(size_t)));
#endif

    return ret;
}

// kmalloc - kfree: implement use-after-free sanitizer?
void kfree(void *addr)
{
    // NULL ptr free is a noop
    if (!addr) return;

    struct page *pg = phys2page((uintptr_t)addr - hhdm->offset);
    if (pg->flags & STRUCT_PAGE_FLAG_KMALLOC_BUDDY) {
        pg->flags &= ~STRUCT_PAGE_FLAG_KMALLOC_BUDDY;
        size_t ord = pg->order;
        pg->order = 0x0;

        // double free check?
        page_free(pg, ord);
        return;
    }

#ifdef CONFIG_SLAB_SANITIZE
    uint64_t *tmp = addr;
    // poison mark this area
    if (*tmp == KMALLOC_DOUBLE_FREE_QWORD) {
        kpanic(0, NULL, "Double free detected on freed object at %p\n", addr);
    } else {
        *tmp = KMALLOC_DOUBLE_FREE_QWORD;
    }

    addr -= KMALLOC_REDZONE_LEFT;
#endif

    struct slab *s = _find_corresponding_slab(addr);

#ifdef CONFIG_SLAB_SANITIZE
    int size = *((size_t *)(addr + s->this_cache->obj_md_size - sizeof(size_t)));

    uint8_t *ptr = addr;
    for (int i = 0; i < KMALLOC_REDZONE_LEFT; i++) {
        if (*ptr != KMALLOC_SANITIZE_BYTE) {
            kpanic(0, NULL, "Buffer underflow detected on slab %p, freed object %p of size %d\n",
                s, addr += KMALLOC_REDZONE_LEFT, size);
        }
        *ptr = 0;
        ptr++;
    }

    ptr = addr + size + KMALLOC_REDZONE_LEFT;
    for (int i = 0; i < KMALLOC_REDZONE_RIGHT + s->this_cache->obj_size - size - (int)sizeof(size_t); i++) {
        if (*ptr != KMALLOC_SANITIZE_BYTE) {
            kpanic(0, NULL, "Buffer overflow detected on slab %p, freed object %p of size %d\n",
                s, addr += KMALLOC_REDZONE_LEFT, size);
        }
        *ptr = 0;
        ptr++;
    }
#endif

    spin_lock(&s->this_cache->lock);

    // add object to slabs freelist
    *((uintptr_t *)addr) = (uintptr_t)s->freelist;
    s->freelist = addr;

    struct slab_cache *c = s->this_cache;
    if (s->used_objs == s->total_objs) {
        // if migration from empty to partial
        _list_unlink(s, &c->empty_slabs);
        c->empty_slab_count--;
        _list_link(s, &c->partial_slabs);
        c->partial_slab_count++;
    } else if (s->used_objs == 1) {
        // if migration from partial to full
        _list_unlink(s, &c->partial_slabs);
        c->partial_slab_count--;
        _list_link(s, &c->full_slabs);
        c->full_slab_count++;

        _attempt_free_full(c);
    } else {
        // nothing
    }

    s->used_objs--;
    c->total_objs_allocated--;

    spin_unlock(&s->this_cache->lock);
}

void *krealloc(void *addr, size_t size)
{
    void *new = kmalloc(size);

    if (addr == NULL)
        return new;

    memcpy(new, addr, size);
    kfree(addr);
    return new;
}

void *kcalloc(size_t entries, size_t size)
{
    void *ret = kmalloc(entries * size);
    memset(ret, 0x00, entries * size);
    return ret;
}

k_spinlock_t templock;
void slab_dbg_print(void)
{
    spin_lock(&templock);
    for (int i = 0; i < KMALLOC_ALLOC_SIZES; i++) {
        struct slab_cache *c = &kmalloc_generic_caches[i];
        spin_lock(&c->lock);

        kprintf("%s, size=%d <obj_per_slab=%lu> <pages_per_slab=%lu>\n"
               "<total_objs=%lu> <total_objs_allocated=%lu> <full=%lu/partials=%lu/empty=%lu>\n",
            c->name, c->obj_size, (c->pages_per_slab * PAGE_SIZE) / c->obj_md_size, c->pages_per_slab,
            c->total_objs, c->total_objs_allocated, c->full_slab_count, c->partial_slab_count, c->empty_slab_count);

        struct slab *curr = c->full_slabs;
        int j = 0;
        kprintf("    full_slabs:\n");
        while (curr) {
            kprintf("        %d: total=%hu, used=%hu\n", j, curr->total_objs, curr->used_objs);
            curr = curr->next;
            j++;
        }

        curr = c->partial_slabs;
        j = 0;
        kprintf("    partial_slabs:\n");
        while (curr) {
            kprintf("        %d: total=%hu, used=%hu\n", j, curr->total_objs, curr->used_objs);
            curr = curr->next;
            j++;
        }

        curr = c->empty_slabs;
        j = 0;
        kprintf("    empty_slabs:\n");
        while (curr) {
            kprintf("        %d: total=%hu, used=%hu\n", j, curr->total_objs, curr->used_objs);
            curr = curr->next;
            j++;
        }

        spin_unlock(&c->lock);
    }
    spin_unlock(&templock);
}