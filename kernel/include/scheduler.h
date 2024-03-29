#pragma once

#include "interrupt.h"
#include "cpu.h"
#include "vmm.h"
#include "process.h"
#include "vector.h"

extern struct task *kernel_task;

#define SCHEDULER_MAX_THREADS

void init_scheduling(void);
struct task *scheduler_add_task(struct task *parent_proc, page_map_ctx *pmc);
thread_t *scheduler_add_kernel_thread(void *entry);
void scheduler_preempt(cpu_ctx_t *regs);
void __attribute__((noreturn)) wait_for_scheduling(void);
void __attribute__((noreturn)) scheduler_kernel_thread_exit(void);

static inline thread_t *get_current_thread(void) {
    return (thread_t *)read_kernel_gs_base();
}

static inline cpu_local_t *get_this_cpu(void)
{
    if (interrupts_enabled())
        kpanic(0, NULL, "It's illegal to get_this_cpu() while IF set\n");
    thread_t *current_thread = get_current_thread();
    return current_thread->cpu;
}