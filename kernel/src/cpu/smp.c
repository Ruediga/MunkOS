#include "smp.h"
#include "limine.h"
#include "liballoc.h"
#include "kprintf.h"
#include "gdt.h"
#include "interrupt.h"
#include "pmm.h"
#include "vmm.h"
#include "cpu.h"
#include "apic.h"

struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0,
    .flags = 0
};

struct limine_smp_response *smp_response = NULL;
cpu_local_t *global_cpus = NULL; // store cpu data for each booted cpu
uint64_t smp_cpu_count = 0;

static volatile size_t startup_checksum = 0;

k_spinlock_t somelock;

static void processor_core_entry(struct limine_smp_info *smp_info)
{
    rld_gdt();
    load_idt();

    vmm_set_ctx(&kernel_pmc);

    cpu_local_t *this_cpu = (cpu_local_t *)smp_info->extra_argument;
    rld_tss(&this_cpu->tss);

    thread_t *idle_thread = kcalloc(1, sizeof(thread_t));
    idle_thread->owner = kernel_task;
    idle_thread->cpu = this_cpu;
    idle_thread->gs_base = idle_thread;
    idle_thread->fs_base = 0;

    this_cpu->idle_thread = idle_thread;

    write_gs_base(idle_thread);
    write_kernel_gs_base(idle_thread);

    acquire_lock(&somelock);
    init_lapic();
    release_lock(&somelock);

    kprintf("  - cpu %lu: lapic_id=%u, bus_frequency=%luMHz booted up\n",
        this_cpu->id, this_cpu->lapic_id, this_cpu->lapic_clock_frequency / 1000000);
    startup_checksum++;

    // if main cpu, return to normal execution
    if (this_cpu->lapic_id == smp_response->bsp_lapic_id) {
        return;
    }

    wait_for_scheduling();
}

/* struct limine_smp_info {
 *     uint32_t processor_id;
 *     uint32_t lapic_id;
 *     uint64_t reserved;
 *     void (*goto_address)(struct limine_smp_info *);
 *     uint64_t extra_argument;
 * }; */
void boot_other_cores(void)
{
    smp_response = smp_request.response;
    smp_cpu_count = smp_response->cpu_count;

    global_cpus = kmalloc(sizeof(cpu_local_t) * smp_cpu_count);

    for (size_t i = 0; i < smp_cpu_count; i++) {
        struct limine_smp_info *smp_info = smp_response->cpus[i];

        global_cpus[i].id = smp_info->processor_id;
        global_cpus[i].lapic_id = smp_info->lapic_id;

        smp_info->extra_argument = (uintptr_t)(&global_cpus[i]);

        if (smp_info->lapic_id == smp_response->bsp_lapic_id) {
            processor_core_entry(smp_info);
            continue;
        }
        smp_info->goto_address = processor_core_entry;
    }

    while (startup_checksum < smp_cpu_count)
        __asm__ ("pause");

    kprintf("  - successfully booted up all %lu cores\n", smp_cpu_count);
}