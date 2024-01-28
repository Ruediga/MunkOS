#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

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
#include "scheduler.h"

LIMINE_BASE_REVISION(1)

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

struct cpuid_data_common cpuid_data = {};
struct flanterm_context *ft_ctx;

extern volatile size_t pit_ticks;
void test_thread(void)
{
    while (1) {
        if ((pit_ticks % 1000))
            kprintf("1 sec passed %lu\n", pit_ticks);
    }

    wait_for_scheduling();
}

void test_thread2(void)
{
    while (1) {
        if ((pit_ticks % 1000))
            kprintf("1 sec passed here too %lu\n", pit_ticks);
    }

    wait_for_scheduling();
}

void kernel_entry(void)
{
    if (LIMINE_BASE_REVISION_SUPPORTED == false || framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        __asm__ volatile ("hlt");
    }

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

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

    kprintf("%s scheduling initialized...\n\r", kernel_okay_string);
    init_scheduling();

    kprintf("%s enabling smp...\n\r", kernel_okay_string);
    boot_other_cores();

    //kpanic(NULL, "Panic test %lu\n", 123456789ul);

    kprintf("%s redirecting pit...\n\r", kernel_okay_string);
    init_pit();

    ps2_init();

    scheduler_add_kernel_thread(test_thread);
    scheduler_add_kernel_thread(test_thread2);

    kprintf("bsp core waiting\n");
    wait_for_scheduling();
}