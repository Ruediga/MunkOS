#include "stacktrace.h"
#include "kprintf.h"
#include "vmm.h"

#include <stdbool.h>

__attribute__((weak)) struct stacktrace_symbol_table_entry stacktrace_symtable[] = {
    {0x0, "INVALID_WEAK"}
};

// takes in a stack frame pointer and tries to resolve the corresponding symbol, return success
static bool stacktrace_analyze_frame(uintptr_t address, size_t which)
{
    // account for KASLR
    uintptr_t kaslr_off = kernel_address->virtual_base - 0xFFFFFFFF80000000;
    address -= kaslr_off;

    // loop through list of (ordered) stack frames,
    // search for biggest symbols address smaller than address
    struct stacktrace_symbol_table_entry *prev = &stacktrace_symtable[0];
    for (size_t i = 0; stacktrace_symtable[i].address; i++) {
        if (address <= stacktrace_symtable[i].address) {
            uintptr_t off = address - prev->address;
            kprintf("    [frame #%lu]: 0x%016lx at <%s() + 0x%lu>\n", which, prev->address, prev->symbol_name, off);
            return true;
        }

        prev = &stacktrace_symtable[i];
    }
    return false;
};

/* sys-v abi specified stack frame layout:
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
void stacktrace_at(uintptr_t rbp)
{
    kprintf("tracing call stack:\n");

    struct stacktrace_rbp_rel_stack_frame *frame = (void *)rbp;

    for(size_t i = 0; frame; i++) {
        if (!frame->rsp || !stacktrace_analyze_frame(frame->rsp, i)) {
            break;
        }

        frame = frame->rbp;
    }
}

void stacktrace()
{
    uintptr_t rbp;  // = (uintptr_t)__builtin_frame_address(0);
    __asm__ volatile ("mov %%rbp, %0" : "=r"(rbp) : : );
    stacktrace_at(rbp);
}