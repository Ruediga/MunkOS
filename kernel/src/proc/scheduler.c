#include "scheduler.h"
#include "kprintf.h"
#include "liballoc.h"
#include "apic.h"
#include "cpu.h"
#include "queue.h"
#include "memory.h"

vector_t proc_list = VECTOR_INIT_FAST(sizeof(struct task *));

queue_t thread_queue = QUEUE_INIT_FAST();

static k_spinlock_t scheduler_lock;

struct task *kernel_task = NULL;

void init_scheduling(void)
{
    interrupts_register_vector(INT_VEC_SCHEDULER, (uintptr_t)scheduler_handler);
    kernel_task = scheduler_add_task(NULL, &kernel_pmc);
}

// initialize a new process:
// alloc resources, add to scheduler
struct task *scheduler_add_task(struct task *parent_proc, page_map_ctx *pmc)
{
    acquire_lock(&scheduler_lock);
    struct task *new_proc = kmalloc(sizeof(new_proc));

    vector_init(&new_proc->threads, sizeof(thread_t));

    if (parent_proc == NULL) {
        // if kernel process
        new_proc->parent = NULL;
        new_proc->pmc = pmc;
    }

    new_proc->pid = vector_append(&proc_list, new_proc);

    release_lock(&scheduler_lock);
    return new_proc;
}

thread_t *scheduler_add_kernel_thread(void *entry)
{
    acquire_lock(&scheduler_lock);
    kprintf("Adding new kernel thread...\n");

    thread_t *new_thread = kmalloc(sizeof(thread_t));

    // [TODO] make sure to deallocate the stack
    vector_init(&new_thread->stacks, sizeof(uintptr_t));

    // stack size ok?
    void *stack_phys = pmm_claim_contiguous_pages(100);
    vector_append(&new_thread->stacks, &stack_phys);
    void *stack = stack_phys + 100 * PAGE_SIZE + hhdm->offset;

    new_thread->context.cs = 0x8;
    new_thread->context.ds = new_thread->context.es = new_thread->context.ss = 0x10;
    new_thread->context.rflags = 0x202; // interrupts enabled
    new_thread->context.rip = (uintptr_t)entry;
    new_thread->context.rdi = 0; // pass args?
    new_thread->context.rsp = (uint64_t)stack;
    new_thread->context.cr3 = (uint64_t)kernel_task->pmc->pml4_address - hhdm->offset;
    new_thread->gs_base = new_thread;

    new_thread->owner = kernel_task;

    queue_enqueue(&thread_queue, (void *)new_thread);

    release_lock(&scheduler_lock);

    return new_thread;
}

__attribute__((noreturn)) void wait_for_scheduling(void)
{
    ints_off();
    lapic_timer_oneshot_us(INT_VEC_SCHEDULER, 20 * 1000);
    ints_on();
    for (;;) __asm__ ("hlt");
    __builtin_unreachable();
}

void scheduler_handler(cpu_ctx_t *regs)
{
    acquire_lock(&scheduler_lock);

    cpu_local_t *this_cpu = get_this_cpu();
    thread_t *this_thread = get_current_thread();

    // save current threads context
    memcpy(&this_thread->context, regs, sizeof(cpu_ctx_t));
    // enqueue thread again
    if (this_thread != this_cpu->idle_thread)
        queue_enqueue(&thread_queue, (void *)this_thread);
    //kprintf("saved context (idle_t?: %i) on core %lu\n", this_thread == this_cpu->idle_thread, get_this_cpu()->id);

    // switch to the next thread
    thread_t *next_thread = (thread_t *)queue_dequeue(&thread_queue);
    if (!next_thread) {
        lapic_timer_oneshot_us(INT_VEC_SCHEDULER, 20 * 1000);
        lapic_send_eoi_signal();

        memcpy(regs, &this_cpu->idle_thread->context, sizeof(cpu_ctx_t));

        write_gs_base(this_cpu->idle_thread->gs_base);
        write_kernel_gs_base(this_cpu->idle_thread->gs_base);
        vmm_set_ctx(this_cpu->idle_thread->owner->pmc);

        //kprintf("idle on core %lu\n", get_this_cpu()->id);

        release_lock(&scheduler_lock);
        return;
    }

    lapic_timer_oneshot_us(INT_VEC_SCHEDULER, 5 * 1000);
    lapic_send_eoi_signal();

    memcpy(regs, &next_thread->context, sizeof(cpu_ctx_t));

    next_thread->cpu = this_thread->cpu;

    write_gs_base(next_thread->gs_base);
    write_kernel_gs_base(next_thread->gs_base);
    vmm_set_ctx(next_thread->owner->pmc);
    this_cpu->tss.rsp0 = (uint64_t)next_thread->kernel_stack;
    //kprintf("rescheduled to core %lu\n", get_this_cpu()->id);

    release_lock(&scheduler_lock);
}