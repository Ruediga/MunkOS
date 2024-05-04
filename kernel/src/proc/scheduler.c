#include "scheduler.h"
#include "kprintf.h"
#include "kheap.h"
#include "apic.h"
#include "cpu.h"
#include "queue.h"
#include "memory.h"
#include "process.h"
#include "compiler.h"

// the scheduler is based on a prio based RR
//
// todo:
//  - cpu pinning
//  - per cpu runqeue
//  - load balancing

VECTOR_TMPL_TYPE(uintptr_t)

// [TODO] use as little as possible
static k_spinlock_t scheduler_big_lock;

struct task *kernel_task = NULL;

static uint64_t scheduler_task_id_counter = 0;

static struct scheduler_runqueue prio_queue[TASK_PRIORITY_MAX_PRIORITIES];
static struct scheduler_runqueue sleep_queue;

static void scheduler_preempt(cpu_ctx_t *regs);

extern void kernel_thread_spinup(void);
extern void load_task_context(struct jmpbuf *context);
// returns 1 on switching back
extern uint64_t save_task_context(struct jmpbuf *context);

static inline struct task *runqueue_pop_front(struct scheduler_runqueue *rq)
{
    spin_lock(&rq->lock);
    if (!rq->head) {
        spin_unlock(&rq->lock);
        return NULL;
    }

    struct task *ret = rq->head;
    if (rq->head == rq->tail) {
        rq->head = rq->tail = NULL;
    } else {
        rq->head = rq->head->rr_next;
        rq->head->rr_prev = NULL;
    }
    spin_unlock(&rq->lock);

    return ret;
}

static inline void runqueue_insert_front(struct scheduler_runqueue *rq, struct task *task) {
    spin_lock(&rq->lock);
    if (!rq->head) {
        rq->head = task;
        rq->tail = task;
    } else {
        task->rr_next = rq->head;
        rq->head->rr_prev = task;
        rq->head = task;
    }
    spin_unlock(&rq->lock);
}

static inline void runqueue_insert_back(struct scheduler_runqueue *rq, struct task *task) {
    spin_lock(&rq->lock);
    if (!rq->head) {
        rq->head = task;
        rq->tail = task;
    } else {
        rq->tail->rr_next = task;
        task->rr_prev = rq->tail;
        rq->tail = task;
    }
    spin_unlock(&rq->lock);
}

static inline void runqueue_remove(struct scheduler_runqueue *rq, struct task *task) {
    if (!task) kpanic(0, NULL, "task == 0");

    spin_lock(&rq->lock);
    if (task == rq->head)
        rq->head = task->rr_next;
    if (task == rq->tail)
        rq->tail = task->rr_prev;
    if (task->rr_prev)
        task->rr_prev->rr_next = task->rr_next;
    if (task->rr_next)
        task->rr_next->rr_prev = task->rr_prev;

    task->rr_next = task->rr_prev = NULL;
    spin_unlock(&rq->lock);
}

// atomically get new task id
static inline uint64_t scheduler_new_tid(void) {
    return __atomic_add_fetch(&scheduler_task_id_counter, 1, __ATOMIC_SEQ_CST);
}

void init_scheduling(void)
{
    interrupts_register_vector(INT_VEC_SCHEDULER, (uintptr_t)scheduler_preempt);
    kernel_task = scheduler_spawn_task(NULL, &kernel_pmc, SPAWN_TASK_NO_KERNEL_STACK, 64 * KiB);
}

// caller responsibilites: upon exiting, task is ...
//  - sleeping (state = TASK_STATE_UNITIALIZED, prio = TASK_PRIORITY_IDLE)
//  - cpu_ctx_t, flags zeroed
//  - stack ptrs are phys
// flags (SPAWN_TASK_):
//  - THREAD_GROUP (inherit gid from parent)
//  - NO_KERNEL_STACK (for kernel threads for example)
struct task *scheduler_spawn_task(struct task *parent_proc, page_map_ctx *pmc, uint8_t flags, uint64_t stacksize)
{
    struct task *new_task = kcalloc(1, sizeof(struct task));

    spin_lock(&scheduler_big_lock);

    new_task->tid = scheduler_new_tid();

    new_task->parent = parent_proc;

    new_task->first_child = NULL;

    if (flags & SPAWN_TASK_THREAD_GROUP) {
        new_task->gid = parent_proc->gid;
        new_task->tg_leader = parent_proc;

        // link into sibling list of parent
        new_task->sibling_prev = NULL;
        new_task->sibling_next = parent_proc->first_child;
        if (parent_proc->first_child)
            parent_proc->first_child->sibling_prev = new_task;
        parent_proc->first_child = new_task;
    } else {
        new_task->gid = new_task->tid;
        new_task->tg_leader = NULL;
        new_task->sibling_prev = NULL;
        new_task->sibling_next = NULL;
    }

    VECTOR_REINIT(new_task->stacks, uintptr_t);

    uintptr_t stack_phys = page2phys(page_alloc(psize2order(stacksize)));
    new_task->stacks.push_back(&new_task->stacks, stack_phys);
    new_task->stack = (void *)(stack_phys + stacksize - 1);

    if (flags & SPAWN_TASK_NO_KERNEL_STACK) {
        new_task->kernel_stack = NULL;
    } else {
        uintptr_t kernel_stack_phys = page2phys(page_alloc(psize2order(KERNEL_STACK_SIZE)));
        new_task->stacks.push_back(&new_task->stacks, kernel_stack_phys);
        new_task->stack = (void *)(kernel_stack_phys + KERNEL_STACK_SIZE - 1);
    }

    // ist stacks ?

    // caller inits cpu_ctx_t
    // caller inits flags
    memset(&new_task->context, 0, sizeof(struct jmpbuf));
    new_task->flags = 0;

    // task gets put to sleep upon creation.
    // caller has to explicitly wake it after initializing it fully.
    new_task->prio = TASK_PRIORITY_IDLE;
    new_task->state = TASK_STATE_UNITIALIZED;
    new_task->rr_next = new_task->rr_prev = NULL;
    runqueue_insert_front(&sleep_queue, new_task);

    new_task->pmc = pmc;

    new_task->krnl_gs_bs = new_task;

    spin_unlock(&scheduler_big_lock);
    return new_task;
}

struct task *scheduler_new_kernel_thread(void (*entry)(void *args), void *args, enum task_priority prio)
{
    (void)entry;

    struct task *thread = scheduler_spawn_task(kernel_task, &kernel_pmc, SPAWN_TASK_THREAD_GROUP, 64 * KiB);

    spin_lock(&scheduler_big_lock);

    thread->prio = prio;

    // map stack into kernel vm
    thread->stack = (void *)((uintptr_t)thread->stack + hhdm->offset);

    // push entry and arg (see task_switch.S)
    thread->stack -= sizeof(uint64_t);
    *((uint64_t *)thread->stack) = (uint64_t)entry;
    thread->stack -= sizeof(uint64_t);
    *((uint64_t *)thread->stack) = (uint64_t)args;

    thread->context.rip = (uint64_t)kernel_thread_spinup;
    thread->context.rsp = (uint64_t)thread->stack;
    thread->context.rbp = (uint64_t)thread->stack;

    thread->context.r12 = thread->context.r13 = thread->context.r14 = thread->context.r15 =
        thread->context.rbx = thread->fs_base = 0;

    thread->flags = TASK_FLAGS_KERNEL_THREAD;
    thread->state = TASK_STATE_SLEEPING;

    spin_unlock(&scheduler_big_lock);

    // enqueue
    scheduler_wake(thread);

    return thread;
}

struct task *scheduler_new_idle_thread()
{
    struct task *thread = scheduler_spawn_task(kernel_task, &kernel_pmc,
        SPAWN_TASK_NO_KERNEL_STACK | SPAWN_TASK_THREAD_GROUP, 64 * KiB);

    spin_lock(&scheduler_big_lock);

    thread->prio = TASK_PRIORITY_IDLE;

    // map stack into kernel vm
    thread->stack = (void *)((uintptr_t)thread->stack + hhdm->offset);

    thread->context.rip = (uintptr_t)kernel_idle;
    thread->context.rsp = (uint64_t)thread->stack;
    thread->context.rbp = (uint64_t)thread->stack;

    thread->context.r12 = thread->context.r13 = thread->context.r14 = thread->context.r15 =
        thread->context.rbx = thread->fs_base = 0;

    thread->flags = TASK_FLAGS_KERNEL_THREAD | TASK_FLAGS_KERNEL_IDLE;
    thread->state = TASK_STATE_READY;

    spin_unlock(&scheduler_big_lock);

    runqueue_remove(&sleep_queue, thread);

    return thread;
}

// only allow active threads to be put to sleep
void scheduler_sleep(struct task *task)
{
    if (task->state == TASK_STATE_SLEEPING)
        kpanic(0, NULL, "Trying to put sleeping task to sleep");

    runqueue_remove(&prio_queue[task->prio], task);
    task->state = TASK_STATE_SLEEPING;
    runqueue_insert_back(&sleep_queue, task);
    kprintf("put thread %lu to sleep\n", task->tid);
}

// allow active and sleeping threads to be woken
void scheduler_wake(struct task *task)
{
    if (task->state != TASK_STATE_SLEEPING)
        kpanic(0, NULL, "Trying to wake non-sleeping task");

    runqueue_remove(&sleep_queue, task);
    task->state = TASK_STATE_READY;
    // put at front so the task spins up as fast as possible
    runqueue_insert_front(&prio_queue[task->prio], task);
    kprintf("waking thread %lu\n", task->tid);
}

// switch currently executed task to target. does not take any queuing or related responsibilites.
comp_noreturn void switch2task(struct task *target)
{
    if (target->state != TASK_STATE_READY)
        kpanic(0, NULL, "cannot switch to non-ready task");

    vmm_set_ctx(target->pmc);
    get_this_cpu()->tss.rsp0 = (uint64_t)target->kernel_stack;

    target->state = TASK_STATE_RUNNING;

    get_this_cpu()->curr_thread = target;

    // dynamic time slice
    lapic_timer_oneshot_us(INT_VEC_SCHEDULER, 20 * 1000);
    lapic_send_eoi_signal();

    load_task_context(&target->context);
    unreachable();
}

struct task *find_task_to_run(void)
{
    struct task *found = NULL;
    // look through runqueues, prioritising higher prio ones
    for (int i = 0; i < TASK_PRIORITY_MAX_PRIORITIES; i++) {
        found = runqueue_pop_front(&prio_queue[i]);
        if (!found) continue;

        if (found->state != TASK_STATE_READY) {
            kpanic(0, NULL, "current implementation disallows non ready tasks in a rq");
            unreachable();
        }

        return found;
    }

    return found;
}

void switch_to_next_task(void)
{
    if (interrupts_enabled())
        kpanic(0, NULL, "interrupts should be off");

    cpu_local_t *this_cpu = get_this_cpu();

    struct task *next_task = find_task_to_run();

    if (!next_task)
        next_task = this_cpu->idle_thread;

    switch2task(next_task);
}

// give up the cpu for one round of scheduling of task->prio
void scheduler_yield(void)
{
    // don't preempt
    ints_off();

    kprintf("yield: this doesnt work yet\n");

    struct task *curr = scheduler_curr_task();

    runqueue_insert_back(&prio_queue[curr->prio], curr);

    lapic_timer_halt();

    ints_on();
    for (;;) __asm__ ("hlt");
}

// state doesnt get saved so dont take locks you don't release
void kernel_idle(void)
{
    ints_on();
    for (;;) __asm__ ("hlt");
    unreachable();
}

void scheduler_preempt(cpu_ctx_t *regs)
{
    (void)regs;
    if (interrupts_enabled())
        kpanic(0, NULL, "this can't happen anyways\n");

    spin_lock(&scheduler_big_lock);

    cpu_local_t *this_cpu = get_this_cpu();
    struct task *curr_task = scheduler_curr_task();

    get_this_cpu()->curr_thread = NULL;

    if (curr_task->state == TASK_STATE_KILLED) {
        kprintf("task %lu is dead\n", curr_task->tid);

        // don't save context or enqueue anymore
        spin_unlock(&scheduler_big_lock);
        switch_to_next_task();
    }

    curr_task->state = TASK_STATE_READY;

    spin_unlock(&scheduler_big_lock);

    // idle task
    if (curr_task == this_cpu->idle_thread) {
        // [TODO] dynamic quantum
        switch_to_next_task();
    }

    // return 1 if return to jmpbuf
    if (save_task_context(&curr_task->context) == 1) {
        return;
    }

    runqueue_insert_back(&prio_queue[curr_task->prio], curr_task);
    switch_to_next_task();
}

void comp_noreturn scheduler_kernel_thread_exit(void)
{
    // fix this bullshit

    ints_off(); // may be called while ints are on

    struct task *curr_task = scheduler_curr_task();

    curr_task->state = TASK_STATE_KILLED;

    runqueue_remove(&prio_queue[curr_task->prio], curr_task);

    switch_to_next_task();

    unreachable();
}