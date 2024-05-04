#pragma once

#include "interrupt.h"
#include "cpu.h"
#include "vmm.h"
#include "process.h"
#include "vector.h"

#define SPAWN_TASK_THREAD_GROUP (1 << 0)
#define SPAWN_TASK_NO_KERNEL_STACK (1 << 1)

#define KERNEL_STACK_SIZE (0x8000)

extern struct task *kernel_task;

struct scheduler_runqueue {
    int num_tasks;
    struct task *head, *tail;   // pick from head, append at tail
    k_spinlock_t lock;
};

void init_scheduling(void);

struct task *scheduler_spawn_task(struct task *parent_proc, page_map_ctx *pmc, uint8_t flags, uint64_t stacksize);
struct task *scheduler_new_kernel_thread(void (*entry)(void *args), void *args, enum task_priority prio);
struct task *scheduler_new_idle_thread();

void kernel_idle(void);
void scheduler_kernel_thread_exit(void);

void scheduler_sleep(struct task *thread);
void scheduler_wake(struct task *task);
void scheduler_yield(void);

void switch_to_next_task(void);
void switch2task(struct task *target);

static inline cpu_local_t *get_this_cpu(void)
{
    if (interrupts_enabled())
        kpanic(0, NULL, "Can't get_this_cpu() while IF set\n");
    return read_kernel_gs_base();
}

static inline struct task *scheduler_curr_task(void) {
    if (interrupts_enabled())
        kpanic(0, NULL, "Can't scheduler_curr_task() while IF set\n");
    return get_this_cpu()->curr_thread;
}