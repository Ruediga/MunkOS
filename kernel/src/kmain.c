#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "std/memory.h"
#include "std/kprintf.h"

// flanterm terminal
#include "flanterm/flanterm.h"
#include "flanterm/backends/fb.h"

#include "gdt/gdt.h"
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

// global flanterm context
struct flanterm_context *ft_ctx;

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

    kprintf("%s framebuffer width: %lu, heigth: %lu\n\r", kernel_okay_string, framebuffer->width, framebuffer->height);

    // load a GDT
    kprintf("%s setting up gdt...\n\r", kernel_okay_string);
    initGDT();

    // initialize IDT
    kprintf("%s enabling interrupts...\n\r", kernel_okay_string);
    initIDT();

    // pmm
    kprintf("%s initializing pmm...\n\r", kernel_okay_string);
    initPMM();

    // vmm
    kprintf("%s initializing vmm && kernel pm...\n\r", kernel_okay_string);
    initVMM();

    // heap
    initializeKernelHeap((0xFFul * 1024ul * 1024ul * 4096ul) / PAGE_SIZE);
    kprintf("%s allocating space for kernel heap...\n\r", kernel_okay_string);

    // put new kernel stack at the top of virtual address space because why not
    allocNewKernelStack(0xFFFF / PAGE_SIZE);

    kprintf("%s parsing acpi tables...\n\r", kernel_okay_string);
    parseACPI();

    struct cpuid_data_common cpuid_data = {};
    cpuid_common(&cpuid_data);

    kprintf("highest supported function: %u\n", cpuid_data.highest_supported_std_func);
    kprintf("vendor: %s\n", cpuid_data.cpu_vendor);
    kprintf("family: %u\n", (uint32_t)cpuid_data.family);
    kprintf("model: %u\n", (uint32_t)cpuid_data.model);
    kprintf("stepping: %u\n", (uint32_t)cpuid_data.stepping);
    kprintf("apic id: %u\n", (uint32_t)cpuid_data.apic_id);
    kprintf("cpu count: %u\n", (uint32_t)cpuid_data.cpu_count);
    kprintf("clfush size: %u\n", (uint32_t)cpuid_data.clflush_size);
    kprintf("brand id: %u\n", (uint32_t)cpuid_data.brand_id);
    kprintf("feature flags (ecx): 0b%032lb\n", (uint64_t)cpuid_data.feature_flags_ecx);
    kprintf("feature flags (edx): 0b%032lb\n", (uint64_t)cpuid_data.feature_flags_edx);
    kprintf("cpu name: %s\n", cpuid_data.cpu_name_string);

    // halt
    asm volatile ("sti");
    kprintf("\n\rDone...");
    asm volatile("loop: jmp loop\n");
}