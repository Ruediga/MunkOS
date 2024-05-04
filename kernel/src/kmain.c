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
#include "frame_alloc.h"
#include "vmm.h"
#include "kheap.h"
#include "acpi.h"
#include "apic.h"
#include "cpu_id.h"
#include "smp.h"
#include "ps2_keyboard.h"
#include "time.h"
#include "scheduler.h"
#include "pci.h"
#include "serial.h"
#include "nvme.h"
#include "stacktrace.h"
#include "compiler.h"

LIMINE_BASE_REVISION(1)

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

struct cpuid_data_common cpuid_data = {};
struct flanterm_context *ft_ctx;

static unsigned long pseudo_rand(unsigned long *seed) {
    *seed = *seed * 1103515245 + 12345;
    return (*seed / 65536) % 32768;
}

// by @NotBonzo
static int stress_test(void) {
    void *ptr = NULL;
    unsigned long sizes[20], i, j, test_cycles = 10000;
    int ret = 0;

    unsigned long seed = 123456789;
    for (i = 0; i < 20; i++) {
        sizes[i] = (pseudo_rand(&seed) % (1024 * 1024 - 1)) + 1;
    }

    for (i = 0; i < test_cycles; i++) {
        if (!(i % 1000))
            kprintf("run %lu\n", i);

        for (j = 0; j < 20; j++) {
            ptr = kmalloc(sizes[j]);
            if (!ptr) {
                kprintf("kalloc failed to allocate memory of size %lu\n", sizes[j]);
                ret = 0xDEAD;
                goto out;
            }

            memset(ptr, 0xaa, sizes[j]);

            ptr = krealloc(ptr, sizes[j] * 2);
            if (!ptr) {
                kprintf("krealloc failed to increase memory size to %lu\n", sizes[j] * 2);
                ret = 0xDEAD;
                goto out;
            }

            memset(ptr, 0x55, sizes[j] * 2);

            kfree(ptr);
            ptr = NULL;

            ptr = kmalloc(sizes[j]);
            if (!ptr) {
                kprintf("kmalloc failed again to allocate memory of size %lu\n", sizes[j]);
                ret = 0xDEAD;
                goto out;
            }

            ptr = krealloc(ptr, sizes[j] / 3);
            if (!ptr) {
                kprintf("krealloc failed to decrease memory size to %lu\n", sizes[j] / 3);
                ret = 0xDEAD;
                goto out;
            }

            kfree(ptr);
            ptr = NULL;
        }
    }

out:
    if (ptr) {
        kfree(ptr);
    }
    return ret;
}

#include "uacpi/uacpi.h"
#include "uacpi/sleep.h"
void init_acpi(void)
{
    uacpi_init_params init_params = {
        .rsdp = (uintptr_t)rsdp_request.response->address - hhdm->offset,
 
        .rt_params = {
            .log_level = UACPI_LOG_TRACE,
            .flags = 0,
        },
    };

    uacpi_status ret = uacpi_initialize(&init_params);
    if (uacpi_unlikely_error(ret)) {
        kpanic(0, NULL, "uACPI failed to initialize: %s", uacpi_status_to_string(ret));
    }

    ret = uacpi_namespace_load();
    if (uacpi_unlikely_error(ret)) {
        kpanic(0, NULL, "uACPI failed to load namespace: %s", uacpi_status_to_string(ret));
    }

    ret = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(ret)) {
        kpanic(0, NULL, "uACPI failed to initialize namespace: %s", uacpi_status_to_string(ret));;
    }

    // uacpi/event.h
    //ret = uacpi_finalize_gpe_initialization();
    //if (uacpi_unlikely_error(ret)) {
    //    kpanic(0, NULL, "uACPI failed to initialize GPE: %s", uacpi_status_to_string(ret));
    //}

    kprintf("%s uACPI initialized\n\r", kernel_okay_string);
 
    // at this point we can do driver stuff and fully use acpi
    // thank @CopyObject abuser
}

void kernel_main(void *args)
{
    ints_on();

    kprintf("i am t0 (main thread), args=%lu\n", args);

    stress_test();

    init_pci();

    kprintf("%s scanned pci(e) bus for devices...\n\r", kernel_okay_string);

    init_acpi();

    rtc_time_ctx_t c = rd_rtc();
    kprintf("UTC: century %hhu, year %hu, month %hhu, "
        "day %hhu, hour %hhu, minute %hhu, second %hhu\n",
        c.century, c.year, c.month, c.day, c.hour, c.minute, c.second);

    for (int i = 0; i < 10; i++) {
        size_t end = system_ticks + 1000;
        while (system_ticks < end) {
            arch_spin_hint();
        }
        kprintf("unix timestamp: %lu\n", unix_time);
    }

    scheduler_kernel_thread_exit();
}cd .git/objects
ls -al
sudo chown -R yourname:yourgroup *

You can tell yourname and yourgroup by:

# for yourname
whoami
# for yourgroup
id -g -n <yourname>


void t2(void)
{
    kprintf("i am t2\n");

    scheduler_kernel_thread_exit();
}

void t3(void)
{
    kprintf("i am t3\n");

    scheduler_kernel_thread_exit();
}

void t1(void)
{
    kprintf("i am t1\n");

    scheduler_kernel_thread_exit();
}

void kernel_entry(void)
{
    if (LIMINE_BASE_REVISION_SUPPORTED == false || framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        __asm__ ("hlt");
    }

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // flanterm (https://github.com/mintsuki/flanterm)
    ft_ctx = flanterm_fb_init(
        NULL, NULL, framebuffer->address, framebuffer->width,
        framebuffer->height, framebuffer->pitch,
        framebuffer->red_mask_size, framebuffer->red_mask_shift,
        framebuffer->green_mask_size, framebuffer->green_mask_shift,
        framebuffer->blue_mask_size, framebuffer->blue_mask_shift,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 1, 0, 0, 0
    );

    init_serial();

    kprintf("Build from %s@%s\n", __DATE__, __TIME__);

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
    allocator_init();

    // vmm
    kprintf("%s initializing vmm && kernel pm...\n\r", kernel_okay_string);
    init_vmm();

    // heap
    kprintf("%s allocating space for kernel heap...\n\r", kernel_okay_string);
    slab_init();

    kprintf("%s parsing acpi tables...\n\r", kernel_okay_string);
    parse_acpi();

    kprintf("%s setting up the ioapic...\n\r", kernel_okay_string);
    init_ioapic();

    kprintf("%s scheduling initialized...\n\r", kernel_okay_string);
    init_scheduling();

    kprintf("%s enabling smp...\n\r", kernel_okay_string);
    boot_other_cores();

    kprintf("%s initializing time...\n\r", kernel_okay_string);
    time_init();

    ps2_init();

    scheduler_new_kernel_thread(kernel_main, NULL, TASK_PRIORITY_NORMAL);

    if (interrupts_enabled())
        kpanic(0, NULL, "bad\n");

    switch_to_next_task();
    unreachable();
}