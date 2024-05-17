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
#include "mmu.h"
#include "kheap.h"
#include "_acpi.h"
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
#include "process.h"
#include "locking.h"
#include "uacpi/kernel_api.h"

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
    kprintf_verbose("%s starting allocator stress test\n", ansi_progress_string);
    void *ptr = NULL;
    unsigned long sizes[20], i, j, test_cycles = 200;
    int ret = 0;

    unsigned long seed = 123456789;
    for (i = 0; i < 20; i++) {
        sizes[i] = (pseudo_rand(&seed) % (1024 * 1024 - 1)) + 1;
    }

    for (i = 0; i < test_cycles; i++) {

        for (j = 0; j < 20; j++) {
            ptr = kcalloc(1, sizes[j]);
            if (!ptr) {
                kprintf("  - kalloc failed to allocate memory of size %lu\n", sizes[j]);
                ret = 0xDEAD;
                goto out;
            }

            memset(ptr, 0xaa, sizes[j]);

            ptr = krealloc(ptr, sizes[j] * 2);
            if (!ptr) {
                kprintf("  - krealloc failed to increase memory size to %lu\n", sizes[j] * 2);
                ret = 0xDEAD;
                goto out;
            }

            memset(ptr, 0x55, sizes[j] * 2);

            kfree(ptr);
            ptr = NULL;

            ptr = kcalloc(1, sizes[j]);
            if (!ptr) {
                kprintf("  - kmalloc failed again to allocate memory of size %lu\n", sizes[j]);
                ret = 0xDEAD;
                goto out;
            }

            ptr = krealloc(ptr, sizes[j] / 3);
            if (!ptr) {
                kprintf("  - krealloc failed to decrease memory size to %lu\n", sizes[j] / 3);
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
            .log_level = UACPI_LOG_ERROR,
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

    kprintf("%s uacpi initialized\n", ansi_okay_string);
 
    // at this point we can do driver stuff and fully use acpi
    // thanks @CopyObject abuser
}

void t2(void *arg)
{
    struct task *curr = scheduler_curr_task();
    preempt_enable();

    kprintf("i am t2: %p (tid=%lu, gid=%lu)\n",
        arg, curr->tid, curr->gid);

    for (int i = 0; i < 5; i++) {
        scheduler_sleep_for(1024 - system_ticks % 1024);
        //kprintf("unix timestamp: %lu\n", unix_time);

        //rtc_time_ctx_t c = rd_rtc();
        //kprintf("UTC: century %hhu, year %hu, month %hhu, "
        //    "day %hhu, hour %hhu, minute %hhu, second %hhu\n",
        //    c.century, c.year, c.month, c.day, c.hour, c.minute, c.second);
    }
    scheduler_new_kernel_thread(t2, NULL, TASK_PRIORITY_NORMAL);

    scheduler_kernel_thread_exit();
}

void kernel_main(void *args)
{
    (void)args;

    preempt_enable();

    if (stress_test()) {
        kprintf("%s allocator stress test failed\n", ansi_failed_string);
    } else {
        kprintf_verbose("%s allocator stress test completed\n", ansi_okay_string);
    }

    init_acpi();

    init_pci();

    vfs_init();

    rtc_time_ctx_t c = rd_rtc();
    kprintf("UTC: century %hhu, year %hu, month %hhu, "
        "day %hhu, hour %hhu, minute %hhu, second %hhu\n",
        c.century, c.year, c.month, c.day, c.hour, c.minute, c.second);

    scheduler_new_kernel_thread(t2, NULL, TASK_PRIORITY_NORMAL);

    for(;;)
        __asm__ volatile ("hlt");

    // if we exit instead of spinning we kill all other kernel tasks with kthread_exit()
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

    kprintf_verbose("%s Build from %s@%s\n", ansi_okay_string, __DATE__, __TIME__);

    kprintf_verbose("%s performing compatibility check...\n", ansi_progress_string);
    cpuid_common(&cpuid_data);
    cpuid_compatibility_check(&cpuid_data);
    if (!memcmp(cpuid_data.cpu_vendor, "GenuineIntel", 13)) {
        // intel only
        kprintf("  - cpuid: %s\n", cpuid_data.cpu_name_string);
    }
    kprintf_verbose("%s system is supported\n", ansi_okay_string);

    kprintf_verbose("%s framebuffer width: %lu, heigth: %lu\n\r", ansi_okay_string, framebuffer->width, framebuffer->height);

    kprintf_verbose("%s initializing architecture specifics...\n", ansi_progress_string);
    init_gdt();
    init_idt();
    kprintf("%s basic architectural setup done\n", ansi_okay_string);

    kprintf_verbose("%s initializing memory manager...\n", ansi_progress_string);
    allocator_init();
    init_vmm();
    slab_init();
    kprintf("%s mm setup done\n", ansi_okay_string);

    parse_acpi();

    init_ioapic();

    init_scheduling();

    boot_other_cores();

    time_init();

    ps2_init();

    scheduler_new_kernel_thread(kernel_main, NULL, TASK_PRIORITY_NORMAL);

    if (preempt_fetch())
        kpanic(0, NULL, "bad\n");

    switch_to_next_task();
    unreachable();
}