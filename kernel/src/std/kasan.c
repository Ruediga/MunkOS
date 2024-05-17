// reference implementation: omar berrow (https://github.com/oberrow/obos)

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "compiler.h"
#include "kprintf.h"
#include "interrupt.h"
#include "memory.h"
#include "frame_alloc.h"

#define CROSSES_PAGE_BOUNDARY(base, size) \
    ((((base) + (size)) % PAGE_SIZE) == ((base) % PAGE_SIZE))

enum asan_violation_type
{
    ASAN_VIOLATION_TYPE_INVALID,
    ASAN_VIOLATION_TYPE_INVALID_ACCESS,
    ASAN_VIOLATION_TYPE_SHADOW_SPACE_ACCESS,
    ASAN_VIOLATION_TYPE_STACK_SHADOW_SPACE_ACCESS,
};

const uint8_t asan_poison = 0b10101010; 
const uint8_t asan_stack_poison = 0b01010101;

bool memcmpagainst(const void *blk1, int against, size_t count) {
    const unsigned char *p = blk1;
    unsigned char value = (unsigned char)against;

    for (size_t i = 0; i < count; ++i) {
        if (p[i] != value) {
            return false;
        }
    }

    return true;
}

void asan_report(void *addr, size_t size, uintptr_t rip,
    bool rw, enum asan_violation_type type, bool abort)
{    
    if (type == ASAN_VIOLATION_TYPE_INVALID) {
        kprintf("asan: invalid type\n");
        return;
    }

    if(type == ASAN_VIOLATION_TYPE_SHADOW_SPACE_ACCESS)
        kprintf("(shadow space) ");
    else if(type == ASAN_VIOLATION_TYPE_SHADOW_SPACE_ACCESS)
        kprintf("(stack) ");

    kprintf("asan: while trying to %s %lu bytes from 0x%p at ip=%p\n",
        rw ? "write" : "read", size, addr, rip);

    if (abort)
        kpanic(0, NULL, "^ fatal asan report");
}

bool is_allocated(uintptr_t base, size_t length)
{
    (void)base;
    (void)length;
    // can't use page->flags here since:
    // 1.) page_alloc() doesn't set them,
    // 2.) non usable areas aren't supposed to fail
    // maybe use page tables instead, but that would mean
    // that we can't direct map everything...
    return true;
}

void asan_shadow_space_access(void *addr, size_t size, uintptr_t rip,
    bool rw, bool abort)
{
    return;
    bool is_poisoned = false;
    bool short_cicuit_first = false, short_circuit_second = false;

    if (CROSSES_PAGE_BOUNDARY((uintptr_t)addr - 16, (uintptr_t)16))
        if ((short_cicuit_first = !is_allocated((uintptr_t)addr - 16, 16)))
            goto sc_1;

    is_poisoned = memcmpagainst((void *)((uintptr_t)addr - 16), asan_poison, 16);

sc_1:
    if (!is_poisoned)
        if (CROSSES_PAGE_BOUNDARY((uintptr_t)addr + 16, (uintptr_t)16))
            if ((short_circuit_second = !is_allocated((uintptr_t)addr + 16, 16)))
                goto sc_2;

    is_poisoned = memcmpagainst((void *)((uintptr_t)addr + 16), asan_poison, 16);

sc_2:
    if (is_poisoned || (short_cicuit_first && short_circuit_second))
        asan_report(addr, size, rip, rw, ASAN_VIOLATION_TYPE_SHADOW_SPACE_ACCESS, abort);
}

void asan_stack_shadow_space_access(void *addr, size_t size, uintptr_t rip,
    bool rw, bool abort)
{
    bool is_poisoned = false;
    bool short_cicuit_first = false, short_circuit_second = false;

    if (CROSSES_PAGE_BOUNDARY((uintptr_t)addr - 8, (uintptr_t)8))
        if ((short_cicuit_first = !is_allocated((uintptr_t)addr - 8, 8)))
            goto sc_1;

    is_poisoned = memcmpagainst((void *)((uintptr_t)addr - 8), asan_poison, 8);

sc_1:
    if (!is_poisoned)
        if (CROSSES_PAGE_BOUNDARY((uintptr_t)addr + size, (uintptr_t)8))
            if ((short_circuit_second = !is_allocated((uintptr_t)addr + size, 8)))
                goto sc_2;

    is_poisoned = memcmpagainst((void *)((uintptr_t)addr + size), asan_poison, 8);

sc_2:
    if (is_poisoned || (short_cicuit_first && short_circuit_second))
        asan_report(addr, size, rip, rw, ASAN_VIOLATION_TYPE_SHADOW_SPACE_ACCESS, abort);
}

void asan_verify(void *addr, size_t size, uintptr_t rip, bool rw, bool abort)
{
    bool crosses_bound = CROSSES_PAGE_BOUNDARY((uintptr_t)addr, (uintptr_t)size);

    if (crosses_bound)
        size += PAGE_SIZE;
    
    // fail here if not allocated

    if (rw && memcmpagainst(addr, asan_poison, size))
        asan_shadow_space_access(addr, size, rip, rw, abort);
    if (!rw && memcmpagainst(addr, asan_stack_poison, size))
        asan_stack_shadow_space_access(addr, size, rip, rw, abort);
}

#if __INTELLISENSE__
#	define __builtin_return_address(n) n
#	define __builtin_extract_return_addr(a) a
#endif

#define ASAN_LOAD_NOABORT(size)\
void __asan_load##size##_noabort(void *addr)\
{\
	asan_verify(addr, size, (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)), false, false);\
}
#define ASAN_LOAD_ABORT(size)\
void __asan_load##size(void *addr)\
{\
	asan_verify(addr, size, (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)), false, true);\
}
#define ASAN_STORE_NOABORT(size)\
void __asan_store##size##_noabort(void *addr)\
{\
	asan_verify(addr, size, (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)), true, false);\
}
#define ASAN_STORE_ABORT(size)\
void __asan_store##size(void *addr)\
{\
	asan_verify(addr, size, (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)), true, true);\
}

ASAN_LOAD_ABORT(1)
ASAN_LOAD_ABORT(2)
ASAN_LOAD_ABORT(4)
ASAN_LOAD_ABORT(8)
ASAN_LOAD_ABORT(16)
ASAN_LOAD_NOABORT(1)
ASAN_LOAD_NOABORT(2)
ASAN_LOAD_NOABORT(4)
ASAN_LOAD_NOABORT(8)
ASAN_LOAD_NOABORT(16)

ASAN_STORE_ABORT(1)
ASAN_STORE_ABORT(2)
ASAN_STORE_ABORT(4)
ASAN_STORE_ABORT(8)
ASAN_STORE_ABORT(16)
ASAN_STORE_NOABORT(1)
ASAN_STORE_NOABORT(2)
ASAN_STORE_NOABORT(4)
ASAN_STORE_NOABORT(8)
ASAN_STORE_NOABORT(16)

void __asan_load_n(void *addr, size_t size)
{
    asan_verify(addr, size, (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)), false, true);
}
void __asan_store_n(void *addr, size_t size)
{
    asan_verify(addr, size, (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)), true, true); 
}
void __asan_loadN_noabort(void *addr, size_t size)
{
    asan_verify(addr, size, (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)), false, false);
}
void __asan_storeN_noabort(void *addr, size_t size)
{
    asan_verify(addr, size, (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)), true, false);
}
void __asan_after_dynamic_init() { return; /* STUB */ }
void __asan_before_dynamic_init() { return; /* STUB */ }
void __asan_handle_no_return() { return; /* STUB */ }