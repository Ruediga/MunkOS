#pragma once

#include <stddef.h>
#include <stdint.h>

struct thread_t;

typedef struct {
    size_t lock;
    size_t dumb_idea;
} k_spinlock_t;

struct task_state_segment {
    uint32_t reserved_0;
    uint64_t rsp0;  // privilege level stacks
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved_1;
    uint64_t ist1;  // additional stack (IDT IST)
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved_2;
    uint16_t reserved_3;
    uint16_t iomba;
};

typedef struct cpu_local_t {
    size_t id;          // core id
    uint32_t lapic_id;  // lapic id of the processor
    uint64_t lapic_clock_frequency;
    struct task_state_segment tss;
    struct thread_t *idle_thread;
} cpu_local_t;

inline uint64_t read_msr(uint32_t reg)
{
    uint32_t eax = 0, edx = 0;
   __asm__ volatile(
        "rdmsr"
        : "=a"(eax), "=d"(edx)
        : "c"(reg)
        :"memory"
    );
   return ((uint64_t)eax | (uint64_t)edx << 32);
}

inline void write_msr(uint32_t reg, uint64_t value)
{
    __asm__ volatile(
        "wrmsr"
        :
        : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)), "c"(reg)
        : "memory"
    );
}

// fs base
inline void write_fs_base(uintptr_t address) {
    write_msr(0xc0000100, address);
}

inline uintptr_t read_fs_base(void) {
    return read_msr(0xc0000100);
}

// gs base
inline void write_gs_base(struct thread_t *address) {
    write_msr(0xc0000101, (uintptr_t)address);
}

inline struct thread_t *read_gs_base(void) {
    return (struct thread_t *)read_msr(0xc0000101);
}

// kernel gs base
inline void write_kernel_gs_base(struct thread_t *address) {
    write_msr(0xc0000102, (uintptr_t)address);
}

inline struct thread_t *read_kernel_gs_base(void) {
    return (struct thread_t *)read_msr(0xc0000102);
}

static inline void ints_off(void) {
    __asm__ ("cli");
}

static inline void ints_on(void) {
    __asm__ ("sti");
}

void acquire_lock(k_spinlock_t *lock);
void release_lock(k_spinlock_t *lock);