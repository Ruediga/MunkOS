#include "smp.h"
#include "cpu_id.h"
#include "limine.h"
#include "kheap.h"
#include "kprintf.h"
#include "gdt.h"
#include "interrupt.h"
#include "frame_alloc.h"
#include "mmu.h"
#include "cpu.h"
#include "apic.h"
#include "time.h"

struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0,
    .flags = 0
};

struct limine_smp_response *smp_response = NULL;
cpu_local_t *global_cpus = NULL; // store cpu data for each booted cpu
uint64_t smp_cpu_count = 0;
int smp_initialized;

static volatile size_t startup_checksum = 0;

k_spinlock_t somelock;

static void processor_core_entry(struct limine_smp_info *smp_info)
{
    rld_gdt();
    load_idt();

    mmu_set_ctx(&kernel_pmc);

    cpu_local_t *this_cpu = (cpu_local_t *)smp_info->extra_argument;
    rld_tss(&this_cpu->tss);

    struct task *idle_thread = scheduler_new_idle_thread();

    this_cpu->idle_thread = idle_thread;

    write_gs_base(0x12345);
    write_kernel_gs_base(this_cpu);

    // NOT lapic id in IA32_TSC_AUX
    if (tscp_supported())
        write_tsc_aux(this_cpu->id);

    /*The APIC timer frequency will be the processor’s bus clock or core crystal clock frequency
    (when TSC/core crystal clock ratio is enumerated in CPUID leaf 0x15) divided by the value
    specified in the divide configuration register.*/

    write_msr(0x1A0, read_msr(0x1A0) & ~(1ul << 22));

    // for some newer cpus, this should be calculateable

    spin_lock_global(&somelock);
    init_lapic();
    spin_unlock_global(&somelock);

    kprintf_verbose("  - cpu %lu: lapic_id=%u, bus_frequency=%lumhz booted up\n",
        this_cpu->id, this_cpu->lapic_id, this_cpu->lapic_clock_frequency / 1000000);
    startup_checksum++;

    // if bsp cpu, return to main task
    if (this_cpu->lapic_id == smp_response->bsp_lapic_id) {
        return;
    }

    switch2task(idle_thread);
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
    kprintf_verbose("%s starting other cores...\n", ansi_progress_string);

    smp_response = smp_request.response;
    smp_cpu_count = smp_response->cpu_count;

    global_cpus = kcalloc(1, sizeof(cpu_local_t) * smp_cpu_count);

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
        arch_spin_hint();

    smp_initialized = 1;
    kprintf("%s successfully booted up all %lu cores\n", ansi_okay_string, smp_cpu_count);
}