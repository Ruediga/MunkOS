#include "scheduler.h"
#include "kprintf.h"
#include "kheap.h"
#include "apic.h"
#include "cpu.h"
#include "locking.h"
#include "macros.h"
#include "memory.h"
#include "process.h"
#include "compiler.h"
#include "time.h"
#include "kevent.h"

// the scheduler is based on a prio RR
//
// [TODO]:
//  - cpu pinning
//  - per cpu runqeue
//  - load balancing
//  - CLEANUP

VECTOR_TMPL_TYPE(uintptr_t)

// [TODO] use as little as possible
static k_spinlock_t scheduler_big_lock;

struct task *kernel_task = NULL;

static uint64_t scheduler_task_id_counter = 0;

static struct scheduler_runqueue prio_queue[TASK_PRIORITY_MAX_PRIORITIES];
static struct scheduler_runqueue sleep_queue;
static struct scheduler_runqueue reap_queue;
static kevent_t reap_event;

void kernel_reaper(void *arg);

static void scheduler_preempt(cpu_ctx_t *regs);

extern void kernel_thread_spinup(void);
extern void load_task_context(struct jmpbuf *context);
// returns 1 on switching back
extern uint64_t save_task_context(struct jmpbuf *context);

static inline struct task *runqueue_pop_front(struct scheduler_runqueue *rq)
{
    spin_lock_global(&rq->lock);
    if (!rq->head) {
        spin_unlock_global(&rq->lock);
        return NULL;
    }

    struct task *ret = rq->head;
    if (rq->head == rq->tail) {
        rq->head = rq->tail = NULL;
    } else {
        rq->head = rq->head->rr_next;
        rq->head->rr_prev = NULL;
    }

    ret->rr_next = ret->rr_prev = NULL;

    rq->num_tasks--;

    spin_unlock_global(&rq->lock);

    return ret;
}

static inline void runqueue_insert_front(struct scheduler_runqueue *rq, struct task *task) {
    if (!task) kpanic(0, NULL, "task == 0");

    spin_lock_global(&rq->lock);
    if (!rq->head) {
        rq->head = task;
        rq->tail = task;
        task->rr_next = NULL;
        task->rr_prev = NULL;
    } else {
        task->rr_prev = NULL;
        task->rr_next = rq->head;
        rq->head->rr_prev = task;
        rq->head = task;
    }

    rq->num_tasks++;

    spin_unlock_global(&rq->lock);
}

static inline void runqueue_insert_back(struct scheduler_runqueue *rq, struct task *task) {
    if (!task) kpanic(0, NULL, "task == 0");

    spin_lock_global(&rq->lock);
    // for some reason, this fails if we check rq->head, and I'm convinced this is
    // not a runqueue_X() implementation issue.
    if (!rq->tail) {
        rq->head = task;
        rq->tail = task;
        task->rr_next = NULL;
        task->rr_prev = NULL;
    } else {
        task->rr_next = NULL;
        rq->tail->rr_next = task;
        task->rr_prev = rq->tail;
        rq->tail = task;
    }

    rq->num_tasks++;

    spin_unlock_global(&rq->lock);
}

static inline void runqueue_remove(struct scheduler_runqueue *rq, struct task *task) {
    if (!task) kpanic(0, NULL, "task == 0");

    spin_lock_global(&rq->lock);
    if (task == rq->head) {
        rq->head = task->rr_next;
        if (!rq->head) rq->tail = NULL;
    }
    else if (task == rq->tail) {
        rq->tail = task->rr_prev;
        if (!rq->tail) rq->head = NULL;
    }
    if (task->rr_prev)
        task->rr_prev->rr_next = task->rr_next;
    if (task->rr_next)
        task->rr_next->rr_prev = task->rr_prev;

    task->rr_next = task->rr_prev = NULL;

    rq->num_tasks--;

    spin_unlock_global(&rq->lock);
}

// atomically get new task id
static inline uint64_t scheduler_new_tid(void) {
    return __atomic_add_fetch(&scheduler_task_id_counter, 1, __ATOMIC_SEQ_CST);
}

void init_scheduling(void)
{
    kprintf_verbose("%s starting kernel task and preemption...\n", ansi_progress_string);

    interrupts_register_vector(INT_VEC_SCHEDULER, (uintptr_t)scheduler_preempt);
    kernel_task = scheduler_spawn_task(NULL, &kernel_pmc, SPAWN_TASK_NO_KERNEL_STACK, 64 * KiB);

    scheduler_new_kernel_thread(kernel_reaper, NULL, TASK_PRIORITY_CRITICAL);

    kprintf("%s scheduling initialized\n\r", ansi_okay_string);
}

// caller responsibilites: upon exiting, task is ...
//  - sleeping (state = TASK_STATE_UNITIALIZED, prio = TASK_PRIORITY_IDLE)
//  - cpu_ctx_t, flags zeroed
//  - stack ptrs are phys
// flags (SPAWN_TASK_):
//  - THREAD_GROUP (inherit gid from parent)
//  - NO_KERNEL_STACK (for kernel threads for example)
struct task *scheduler_spawn_task(struct task *parent_proc, page_map_ctx_t *pmc, uint8_t flags, uint64_t stacksize)
{
    struct task *new_task = kcalloc(1, sizeof(struct task));

    spin_lock_global(&scheduler_big_lock);

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

    // make sure that the threads task is the first to be pushed
    new_task->stack_size = stacksize;
    uintptr_t stack_phys = page2phys(page_alloc(psize2order(new_task->stack_size)));
    new_task->stacks.push_back(&new_task->stacks, stack_phys);
    new_task->stack = (void *)(stack_phys + new_task->stack_size - 1);

    if (flags & SPAWN_TASK_NO_KERNEL_STACK) {
        new_task->kernel_stack = NULL;
    } else {
        uintptr_t kernel_stack_phys = page2phys(page_alloc(psize2order(KERNEL_STACK_SIZE)));
        new_task->stacks.push_back(&new_task->stacks, kernel_stack_phys);
        new_task->stack = (void *)(kernel_stack_phys + KERNEL_STACK_SIZE);
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

    spin_unlock_global(&scheduler_big_lock);
    return new_task;
}

// cleanup a task from the reap queue. invoked from the schedulers
// task killer worker thread
void scheduler_cleanup_task(struct task *task)
{
    if (task->state != TASK_STATE_KILLED)
        kpanic(0, NULL, "trying to cleanup non-dead task");

    if (task->rr_next || task->rr_prev)
        // only call cleanup_task() when we've already been popped from the reap queue
        kpanic(0, NULL, "we shouldn't be enlinked here");

    // unlink from sibling list (maybe disable ints here?)
    spin_lock_global(&scheduler_big_lock);

    // kill all children
    struct task *child = task->first_child;
    while (child) {
        child->state = TASK_STATE_KILLED;
        child = child->sibling_next;
    }

    // we are the first child
    if (task->parent->first_child == task)
        task->parent->first_child = task->sibling_next;

    if (task->sibling_next)
        task->sibling_next->sibling_prev = task->sibling_prev;
    if (task->sibling_prev)
        task->sibling_prev->sibling_next = task->sibling_next;

    spin_unlock_global(&scheduler_big_lock);

    // threads task is the first to be pushed
    page_free(phys2page(task->stacks.data[0]), psize2order(task->stack_size));

    // clean up all IST and kernel stacks
    for (size_t i = 1; i < task->stacks.size; i++) {
        page_free(phys2page(task->stacks.data[i]), psize2order(KERNEL_STACK_SIZE));
    }

    // [TODO] clean up page map context
    // cleanup(task->pmc);
}

struct task *scheduler_new_kernel_thread(void (*entry)(void *args), void *args, enum task_priority prio)
{
    (void)entry;

    struct task *thread = scheduler_spawn_task(kernel_task, &kernel_pmc, SPAWN_TASK_THREAD_GROUP, 64 * KiB);

    spin_lock_global(&scheduler_big_lock);

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

    spin_unlock_global(&scheduler_big_lock);

    // enqueue
    scheduler_attempt_wake(thread);

    return thread;
}

struct task *scheduler_new_idle_thread()
{
    struct task *thread = scheduler_spawn_task(kernel_task, &kernel_pmc,
        SPAWN_TASK_NO_KERNEL_STACK | SPAWN_TASK_THREAD_GROUP, 64 * KiB);

    spin_lock_global(&scheduler_big_lock);

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

    spin_unlock_global(&scheduler_big_lock);

    runqueue_remove(&sleep_queue, thread);

    return thread;
}

// only inserts task into sleep list!
// if we want to also switch task if we put the current task to sleep, call yield 
void scheduler_put_task2sleep(struct task *task)
{
    if (!task || preempt_fetch()) kpanic(0, NULL, "task == 0");

    if (task->state == TASK_STATE_SLEEPING)
        kpanic(0, NULL, "Trying to put sleeping task to sleep");

    if (task != get_this_cpu()->curr_thread)
        // make sure to NOT remove from runlist if running task, since we wouldn't be enqueued anyways
        runqueue_remove(&prio_queue[task->prio], task);

    task->state = TASK_STATE_SLEEPING;
    runqueue_insert_back(&sleep_queue, task);
}

// this does NOT fail when trying to wake ready or running threads!
void scheduler_attempt_wake(struct task *task)
{
    if (task->state == TASK_STATE_SLEEPING) {
        runqueue_remove(&sleep_queue, task);
        task->state = TASK_STATE_READY;
        // put at front so the task spins up as fast as possible
        runqueue_insert_front(&prio_queue[task->prio], task);
    }
}

// switch currently executed task to target. does not take any queuing or related responsibilites.
comp_noreturn void switch2task(struct task *target)
{
    if (target->state != TASK_STATE_READY || preempt_fetch())
        kpanic(0, NULL, "cannot switch to non-ready task");

    mmu_set_ctx(target->pmc);
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

// this doesnt save context, it just finds a runnable task and switches to it
void switch_to_next_task(void)
{
    if (preempt_fetch())
        kpanic(0, NULL, "interrupts should be off");

    cpu_local_t *this_cpu = get_this_cpu();

    struct task *next_task = find_task_to_run();

    if (!next_task)
        next_task = this_cpu->idle_thread;
    
    switch2task(next_task);
}

void scheduler_preempt(cpu_ctx_t *regs)
{
    (void)regs;
    if (preempt_fetch())
        kpanic(0, NULL, "this can't happen anyways\n");

    spin_lock_global(&scheduler_big_lock);

    cpu_local_t *this_cpu = get_this_cpu();
    struct task *curr_task = scheduler_curr_task();

    get_this_cpu()->curr_thread = NULL;

    if (curr_task->state == TASK_STATE_KILLED) {
        kprintf("task %lu ready for reaping\n", curr_task->tid);

        runqueue_insert_back(&reap_queue, curr_task);
        kevent_launch(&reap_event);

        // don't save context or enqueue anymore
        spin_unlock_global(&scheduler_big_lock);
        switch_to_next_task();
    }

    curr_task->state = TASK_STATE_READY;

    spin_unlock_global(&scheduler_big_lock);

    // idle task
    if (curr_task == this_cpu->idle_thread) {
        // don't save context
        switch_to_next_task();
    }

    // return 1 if return to jmpbuf
    if (save_task_context(&curr_task->context) == 1) {
        return;
    }

    runqueue_insert_back(&prio_queue[curr_task->prio], curr_task);
    switch_to_next_task();
}

// puts the current thread to sleep
void scheduler_sleep_for(size_t ms)
{
    struct ktimer_node timer;
    register_system_timer(&timer, ms);

    struct kevent_t *events[1] = {&timer.event};

    int ret = kevents_poll(events, 1);

    if (ret != 0)
        kpanic(0, NULL, "wrong events index (%d) triggered", ret);

    return;
}

// give up the cpu for one round of scheduling of task->prio
// if the current thread has been put to sleep, only reschedules
void scheduler_yield(void)
{
    // don't preempt
    preempt_disable();

    struct task *curr = scheduler_curr_task();

    // return 1 if return to jmpbuf
    if (save_task_context(&curr->context) == 1) {
        return;
    }

    if (curr->state != TASK_STATE_SLEEPING) {
        // if sleeping, should already be in sleepqueue,
        // and if not, it should not be enqueued at all
        if (curr->rr_next || curr->rr_prev)
            kpanic(0, NULL, "task %lu shouldn't be enqueued state: %d\n", curr->tid, curr->state);
        runqueue_insert_back(&prio_queue[curr->prio], curr);
    }

    switch_to_next_task();
}

void comp_noreturn scheduler_kernel_thread_exit(void)
{
    preempt_disable(); // may be called while ints are on

    struct task *curr_task = scheduler_curr_task();

    // we shouldn't have to unlink this task from anywhere since we
    // shouldn't be in any runqueue while we're running.

    curr_task->state = TASK_STATE_KILLED;
    runqueue_insert_back(&reap_queue, curr_task);
    kevent_launch(&reap_event);

    switch_to_next_task();

    unreachable();
}

// state doesnt get saved so dont take locks you don't release
void kernel_idle(void)
{
    preempt_enable();
    for (;;) __asm__ ("hlt");
    unreachable();
}

// this thread looks for processes in the reap queue, and cleans them up accordingly
void kernel_reaper(void *arg)
{
    (void)arg;

    preempt_enable();

    kevent_t *events[] = {&reap_event};

    for (;;) {
        int ret = kevents_poll(events, 1);
        if (ret != 0)
            kpanic(0, NULL, "something went wrong");

        struct task *task_to_kill = runqueue_pop_front(&reap_queue);
        kprintf("killing task.tid=%d\n", task_to_kill->tid);
        scheduler_cleanup_task(task_to_kill);
    }

    unreachable();
}