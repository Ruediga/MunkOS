#pragma once

#include "vmm.h"
#include "vector.h"
#include "interrupt.h"
#include "gdt.h"

#include <stdbool.h>

enum task_state {
    NONE
};

struct task {
    int pid;                // unique identifier
    enum task_state state;
    page_map_ctx *pmc;      // this tasks paging structures
    vector_t threads;       // threads
    vector_t children;      // cps
    struct task *parent;
    tss tss;
    // fds, scheduling info
};

typedef struct {
    struct task *owner;     // what task does this thread belong to
    bool schedule;
    vector_t stacks;
    void *kernel_stack;
    struct cpu *cpu;        // thread runs here
    cpu_ctx_t context;      // registers
    uintptr_t gs_base;
    uintptr_t fs_base;
} thread_t;