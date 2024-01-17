#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

// flanterm terminal
#include "flanterm/flanterm.h"
#include "flanterm/backends/fb.h"

#include "memory.h"
#include "kprintf.h"
#include "gdt.h"
#include "interrupt.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "liballoc.h"
#include "acpi.h"
#include "apic.h"
#include "cpu_id.h"
#include "smp.h"
#include "ps2_keyboard.h"
#include "pit.h"

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

    kprintf("%s setting up the ioapic...\n\r", kernel_okay_string);
    init_ioapic();

    kprintf("%s redirecting pit...\n\r", kernel_okay_string);
    init_pit();

    kprintf("%s enabling smp...\n\r", kernel_okay_string);
    boot_other_cores();

    ps2_init();

    kprintf("\n\rDone...");

    kpanic(NULL, 0, "Panic test %lu\n", 123456789ul);

    __asm__ volatile (
        "idle:\n"
        "sti\n"
        "hlt\n"
        "jmp idle\n"
    );
}