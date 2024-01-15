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
#include "apic/ioapic.h"
#include "apic/lapic.h"
#include "cpu/smp.h"

#include "driver/ps2_keyboard.h"

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
    __asm__ ("cli");
    for (;;)
    {
        __asm__ ("hlt");
    }
}

// global cpuid data
struct cpuid_data_common cpuid_data = {};

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

    __asm__ volatile("cli");

    // flanterm (https://github.com/mintsuki/flanterm)
    ft_ctx = flanterm_fb_simple_init(
        framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch);

    kprintf("%s performing compatibility check...\n\r", kernel_okay_string);
    cpuid_common(&cpuid_data);
    cpuid_compatibility_check(&cpuid_data);
    if (!memcmp(cpuid_data.cpu_vendor, "GenuineIntel", 13)) {
        // intel only
        kprintf("  - cpuid: %s\n", cpuid_data.cpu_name_string);
    }

    kprintf("%s framebuffer width: %lu, heigth: %lu\n\r", kernel_okay_string, framebuffer->width, framebuffer->height);

    kprintf("%s setting up gdt...\n\r", kernel_okay_string);
    init_gdt();

    kprintf("%s enabling interrupts...\n\r", kernel_okay_string);
    init_idt();

    // pmm
    kprintf("%s initializing pmm...\n\r", kernel_okay_string);
    init_pmm();

    // vmm
    kprintf("%s initializing vmm && kernel pm...\n\r", kernel_okay_string);
    init_vmm();

    // heap
    init_kernel_heap((0xFFul * 1024ul * 1024ul * 4096ul) / PAGE_SIZE);
    kprintf("%s allocating space for kernel heap...\n\r", kernel_okay_string);

    kprintf("%s parsing acpi tables...\n\r", kernel_okay_string);
    parse_acpi();

    kprintf("%s enabling smp...\n\r", kernel_okay_string);
    boot_other_cores();

    kprintf("%s setting up the ioapic...\n\r", kernel_okay_string);
    init_lapic();
    init_ioapic();

    ps2_init();

    kprintf("\n\rDone...");

    __asm__ volatile (
        "idle:\n"
        "sti\n"
        "hlt\n"
        "jmp idle\n"
    );
}