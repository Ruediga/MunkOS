#include "stacktrace.h"
#include "kprintf.h"

#include <stdbool.h>

__attribute__((weak)) struct stacktrace_symbol_table_entry stacktrace_symtable[] = {
    {0x0, "INVALID_WEAK"}
};

// takes in a stack frame pointer and tries to resolve the corresponding symbol, return success
static inline bool stacktrace_analyze_frame(uintptr_t address)
{
    // loop through list of (ordered) stack frames
    for (size_t i = 0; stacktrace_symtable[i].address; i++) {
        kprintf("table.addr=0x%p, addr=0x%p\n", stacktrace_symtable[i].address, address);

        if (0xFFFFFFFF80000000 >= 0xFFFFFFFF8000870C) {
            kprintf("0x%p >= 0x%p\n", stacktrace_symtable[i].address, address);

            uintptr_t off = address - stacktrace_symtable[i].address;
            kprintf("#%lu: 0x%p @ <%s() + %lX>\n",
                i, stacktrace_symtable[i].address, stacktrace_symtable[i].symbol_name, off);
            return true;
        }
    }
    kprintf("#x: failed to locate symbol 0x%p\n", address);
    return false;
};

inline void stacktrace_at(uintptr_t rbp)
{
    kprintf("Beginning stacktrace...\n");
    /*
     * sys-v abi specified stack frame layout:
     * 
     * |----------------|---------------------------|-----------|
     * | Position       | Contents                  | Frame     |
     * |----------------|---------------------------|-----------|
     * | 8n+16(%rbp)    | memory argument qword n   |           |
     * |                | . . .                     | Previous  |
     * | 16(%rbp)       | memory argument qword 0   |           |
     * |----------------|---------------------------|-----------|
     * | 8(%rbp)        | return address            |           |
     * | 0(%rbp)        | previous %rbp value       |           |
     * | -8(%rbp)       | unspecified               | Current   |
     * |                | . . .                     |           |
     * | 0(%rsp)        | variable size             |           |
     * | -128(%rsp)     | red zone                  |           |
     * |----------------|---------------------------|-----------|
    */
    uintptr_t *base_ptr = (uintptr_t *)rbp;
    __asm__ volatile ("mov %%rbp, %0" : "=g"(base_ptr) :: "memory");
    while (1) {
        // 8(%rbp)
        uintptr_t *return_address = (uintptr_t *)base_ptr[1];

        if (!return_address) break;

        if (!stacktrace_analyze_frame((uintptr_t)return_address)) {
            break;
        }

        // 0(%rbp)#
        base_ptr = (uintptr_t *)base_ptr[0];
    }
}

void stacktrace()
{
    uintptr_t rbp = 0;
    //__asm__ volatile ("mov %%rbp, %0" : "=g"(rbp) :: "memory");
    stacktrace_at(rbp);
}