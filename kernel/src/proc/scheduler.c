#include "scheduler.h"
#include "kprintf.h"
#include "kheap.h"
#include "apic.h"
#include "cpu.h"
#include "queue.h"
#include "memory.h"
#include "process.h"

VECTOR_TMPL_TYPE(thread_t_ptr)
VECTOR_TMPL_TYPE(task_ptr)
VECTOR_TMPL_TYPE(void_ptr)

vector_task_ptr_t proc_list = VECTOR_INIT(task_ptr);

queue_t thread_queue = QUEUE_INIT_FAST();

static k_spinlock_t scheduler_lock;

struct task *kernel_task = NULL;

void init_scheduling(void)
{
    interrupts_register_vector(INT_VEC_SCHEDULER, (uintptr_t)scheduler_preempt);
    kernel_task = scheduler_add_task(NULL, &kernel_pmc);
}

// initialize a new process:
// alloc resources, add to scheduler
struct task *scheduler_add_task(struct task *parent_proc, page_map_ctx *pmc)
{
    acquire_lock(&scheduler_lock);
    struct task *new_proc = kmalloc(sizeof(struct task));

    VECTOR_REINIT(new_proc->threads, thread_t_ptr);

    if (parent_proc == NULL) {
        // if kernel process
        new_proc->parent = NULL;
        new_proc->pmc = pmc;
    }
    //kprintf("1 vec=%p, data=%p, size=%p, cap=%lu, elem=%lu\n", &new_proc->threads, new_proc->threads.data,
    //    new_proc->threads._size, new_proc->threads._capacity, new_proc->threads._element_size);
    //kprintf("1 vec=%p, data=%p, size=%p, cap=%lu, elem=%lu\n", &new_proc->threads, new_proc->threads.data,
    //    new_proc->threads._size, new_proc->threads._capacity, new_proc->threads._element_size);

    //new_proc->pid = vector_append(&proc_list, (void *)&new_proc);
    //kprintf("1 vec=%p, data=%p, size=%p, cap=%lu, elem=%p\n", &new_proc->threads, new_proc->threads.data,
    //    new_proc->threads._size, new_proc->threads._capacity, new_proc->threads._element_size);

    release_lock(&scheduler_lock);
    return new_proc;
}

thread_t *scheduler_add_kernel_thread(void *entry)
{
    acquire_lock(&scheduler_lock);
    kprintf("Adding new kernel thread...\n");

    thread_t *new_thread = kcalloc(1, sizeof(thread_t));

    VECTOR_REINIT(new_thread->stacks, void_ptr);

    // [TODO] proper system
    void *stack_phys = page_alloc_temp(psize2order(100 * PAGE_SIZE));
    new_thread->stacks.push_back(&new_thread->stacks, stack_phys);
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

    kprintf("ktask->tid = %lu (%p)\n", kernel_task->threads.push_back(&kernel_task->threads, new_thread), new_thread);

    queue_enqueue(&thread_queue, (void *)new_thread);

    release_lock(&scheduler_lock);

    return new_thread;
}

// give up the cpu for one full round of scheduling
void scheduler_yield(void)
{
    // ints should be off

    cpu_local_t *this_cpu = get_this_cpu();

    lapic_timer_halt();

    // reschedule this core
    lapic_send_ipi(this_cpu->lapic_id, INT_VEC_SCHEDULER, ICR_DEST_SELF);

    ints_on();
    for (;;) __asm__ ("hlt");
}

__attribute__((noreturn)) void wait_for_scheduling(void)
{
    lapic_timer_oneshot_us(INT_VEC_SCHEDULER, 20 * 1000);
    ints_on();
    for (;;) __asm__ ("hlt");
    __builtin_unreachable();
}

void scheduler_preempt(cpu_ctx_t *regs)
{
    acquire_lock(&scheduler_lock);

    cpu_local_t *this_cpu = get_this_cpu();
    thread_t *this_thread = get_current_thread();

    // cleanup
    if (this_thread->killed) {
        size_t index = this_thread->owner->threads.find(&this_thread->owner->threads, this_thread);
        if (index == VECTOR_NOT_FOUND) {
            kpanic(0, NULL, "Trying to free dead or non existing thread (%p)\n", this_thread);
        }
        this_thread->owner->threads.remove(&this_thread->owner->threads, index);

        for (size_t i = 0; i < this_thread->stacks.size; i++) {
            // [FIXME]
            page_free_temp((void *)((uintptr_t)this_thread->stacks.data[i]), psize2order(100 * PAGE_SIZE));
        }

        this_thread->stacks.reset(&this_thread->stacks);

        kfree(this_thread);

        kprintf("freed thread (%p) successfully\n", this_thread);

        goto killed;
    }

    // save current threads context
    memcpy(&this_thread->context, regs, sizeof(cpu_ctx_t));
    // enqueue thread again
    if (this_thread != this_cpu->idle_thread) {
        queue_enqueue(&thread_queue, (void *)this_thread);
    }
    //kprintf("saved context (idle_t?: %i) on core %lu\n", this_thread == this_cpu->idle_thread, get_this_cpu()->id);

killed:
    // switch to the next thread
    thread_t *next_thread = (thread_t *)queue_dequeue(&thread_queue);
    if (!next_thread) {
        memcpy(regs, &this_cpu->idle_thread->context, sizeof(cpu_ctx_t));

        write_gs_base(this_cpu->idle_thread->gs_base);
        write_kernel_gs_base(this_cpu->idle_thread->gs_base);
        vmm_set_ctx(this_cpu->idle_thread->owner->pmc);

        lapic_timer_oneshot_us(INT_VEC_SCHEDULER, 20 * 1000);
        lapic_send_eoi_signal();

        //kprintf("idle on core %lu\n", get_this_cpu()->id);

        release_lock(&scheduler_lock);
        return;
    }

    memcpy(regs, &next_thread->context, sizeof(cpu_ctx_t));

    next_thread->cpu = this_cpu;

    write_gs_base(next_thread->gs_base);
    write_kernel_gs_base(next_thread->gs_base);
    vmm_set_ctx(next_thread->owner->pmc);
    this_cpu->tss.rsp0 = (uint64_t)next_thread->kernel_stack;
    //kprintf("rescheduled to core %lu\n", get_this_cpu()->id);

    lapic_timer_oneshot_us(INT_VEC_SCHEDULER, 5 * 1000);
    lapic_send_eoi_signal();

    release_lock(&scheduler_lock);
}

void __attribute__((noreturn)) scheduler_kernel_thread_exit(void)
{
    ints_off(); // may be called while ints are on

    struct thread_t *this_thread = get_current_thread();
    this_thread->killed = true;

    scheduler_yield();

    __builtin_unreachable();
}