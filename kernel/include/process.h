#pragma once

#include "vmm.h"
#include "vector.h"
#include "interrupt.h"
#include "gdt.h"

#include <stdbool.h>

struct cpu_local_t;
struct task;
struct task;

VECTOR_DECL_TYPE(uintptr_t)

enum task_flags {
    TASK_FLAGS_KERNEL_THREAD = (1 << 0),    // never gonna go into ring 3
    TASK_FLAGS_KERNEL_IDLE = (1 << 1)
};

enum task_state {
    TASK_STATE_UNITIALIZED,
    TASK_STATE_RUNNING,     // currently active
    TASK_STATE_READY,       // waiting for cpu time
    TASK_STATE_SLEEPING,    // awaiting an event (resource)
    TASK_STATE_KILLED       // cleanup pending
};

enum task_priority {
    TASK_PRIORITY_CRITICAL,     // immediately
    TASK_PRIORITY_HIGH,
    TASK_PRIORITY_NORMAL,       // normal processes
    TASK_PRIORITY_LOW,
    TASK_PRIORITY_IDLE,         // only if nothing else runs
    TASK_PRIORITY_MAX_PRIORITIES
};

// used for setjmp-like context switching
struct jmpbuf {   // offsets:
    uint64_t rip;       // 0x0
    uint64_t rsp;       // 0x8
    uint64_t rbp;       // 0x10

    uint64_t rbx;       // 0x18
    uint64_t r12;       // 0x20
    uint64_t r13;       // 0x28
    uint64_t r14;       // 0x30
    uint64_t r15;       // 0x38
};

// dont forget to update scheduler init stuff
struct task {
    // task identification and relationships
    // =====================================
    int tid;                    // task id - globally unique
    int gid;                    // thread group id - same for tasks in a thread group

    struct task *tg_leader;     // thread group "leader" (first task with this tgid)
    struct task *parent;

    // children with the same parent are linked into sibling list
    struct task *first_child;
    struct task *sibling_prev, *sibling_next;

    // [TODO] name

    // context switch related stuff
    // ============================
    vector_uintptr_t_t stacks;   // all phys stack buffers related to this task (ists, kernel, common)
    void *kernel_stack;         // user mode tasks: rsp0, kernel tasks: unused
    void *stack;                // the tasks thread - also a kernel threads common int thread

    page_map_ctx *pmc;

    struct jmpbuf context;

    // more context
    uint64_t fs_base;   // tls: save per thread

    struct task *krnl_gs_bs;

    // statistics
    // ==========

    // state and sched algorithm
    // =========================
    enum task_flags flags;

    enum task_state state;

    enum task_priority prio;

    struct task *rr_next, *rr_prev;
};