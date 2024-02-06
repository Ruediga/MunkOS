#pragma once

#include "vmm.h"
#include "vector.h"
#include "interrupt.h"
#include "gdt.h"

#include <stdbool.h>

struct cpu_local_t;
struct thread_t;
struct task;

typedef struct thread_t * thread_t_ptr;
typedef struct task * task_ptr;
typedef void * void_ptr;
VECTOR_DECL_TYPE(thread_t_ptr)
VECTOR_DECL_TYPE(task_ptr)
VECTOR_DECL_TYPE(void_ptr)

enum task_state {
    NONE
};

struct task {
    int pid;                // unique identifier
    enum task_state state;
    page_map_ctx *pmc;      // this tasks paging structures
    vector_thread_t_ptr_t threads;   // pointers to threads
    vector_task_ptr_t children;      // child proc pointers
    struct task *parent;
    tss tss;
    // fds, scheduling info
};

typedef struct thread_t {
    struct task *owner;         // what task does this thread belong to
    vector_void_ptr_t stacks;            // pointers to all stacks associated w/ this thread
    void *kernel_stack;
    struct cpu_local_t *cpu;    // thread runs here
    cpu_ctx_t context;          // registers
    struct thread_t *gs_base;
    uintptr_t fs_base;
    bool killed;                // set when exiting
} thread_t;