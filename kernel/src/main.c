#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "std/memory.h"
#include "std/kprintf.h"

// flanterm terminal
#include "flanterm/flanterm.h"
#include "flanterm/backends/fb.h"

#include "gdt.h"
#include "interrupt/idt.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "dynmem/kheap.h"
#include "dynmem/liballoc.h"
#include "acpi/acpi.h"
#include "cpu/cpuid.h"

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

LIMINE_BASE_REVISION(1)

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, in C, they should
// NOT be made "static".

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// Halt and catch fire function.
void hcf(void)
{
    asm("cli");
    for (;;)
    {
        asm("hlt");
    }
}

void allocNewKernelStack(size_t new_stack_size_pages)
{
    for (size_t addr = 0xFFFFFFFFFFFFFFFF; addr > 0xFFFFFFFFFFFFFFFF - (new_stack_size_pages * PAGE_SIZE); addr -= PAGE_SIZE) {
        void *new_page = pmmClaimContiguousPages(1);
        mapPage(&kernel_pmc, addr, (uintptr_t)new_page, PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
    }

    asm volatile (
        "movq $0xFFFFFFFFFFFFFFFF, %%rsp\n"
        : : : "memory"
    );
}

// global flanterm context
struct flanterm_context *ft_ctx;

void kernel_entry(void)
{
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    asm volatile("cli");

    // init flanterm (https://github.com/mintsuki/flanterm)
    ft_ctx = flanterm_fb_simple_init(
        framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch);

    kprintf("Limine framebuffer width: %lu, heigth: %lu\n\r", framebuffer->width, framebuffer->height);

    // load a GDT
    initGDT();
    kprintf("GDT set up\n\r");
   
    // initialize IDT
    initIDT();
    kprintf("Interrupts initialized\n\r");

    // pmm
    initPMM();
    kprintf("PMM functions prepared\n\r");

    // vmm
    initVMM();
    kprintf("VMM and kernel pagemap initialized\n\r");

    // put new kernel stack at the top of virtual address space because why not
    allocNewKernelStack(0xFFFF / PAGE_SIZE);

    // heap
    initializeKernelHeap(0xFFFFFFF / PAGE_SIZE);
    kprintf("Kernel heap initialized\n\r");

    const size_t arr_size = 999;
    uint64_t *ptr_array[arr_size];
    for (size_t i = 0; i < arr_size; i++) {
        ptr_array[i] = (uint64_t *)kmalloc(arr_size * sizeof(uint64_t));
        if (!ptr_array[i]) {
            kprintf("Allocation failed [dbg from kernel_main()]\n");
            continue;
        }
        for (size_t j = 0; j < arr_size - 1; j++) {
            ptr_array[i][j] = 88;
        }
    }
    for (int i = arr_size; i >= 0; i--) {
        kfree((void *)ptr_array[i]);
    }

    parseACPI();
    kprintf("Parsed ACPI tables\n\r");

    char vendor[13];
    cpuid_getCpuVendor(vendor);
    kprintf("CPU Vendor: %s\n", vendor);

    kprintf("%p\n", vendor);

    /*asm volatile (
        "movq $50, %rdx\n"
        "xor %rax, %rax\n"
        "div %rdx\n"
    );*/

    // halt
    kprintf("Done...");
    asm volatile("loop: jmp loop\n");
}