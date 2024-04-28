#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t int_status_t;

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

static inline uint64_t read_msr(uint32_t reg)
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

static inline void write_msr(uint32_t reg, uint64_t value)
{
    __asm__ volatile(
        "wrmsr"
        :
        : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)), "c"(reg)
        : "memory"
    );
}

// fs base
static inline void write_fs_base(uintptr_t address) {
    write_msr(0xc0000100, address);
}

static inline uintptr_t read_fs_base(void) {
    return read_msr(0xc0000100);
}

// gs base
static inline void write_gs_base(struct thread_t *address) {
    write_msr(0xc0000101, (uintptr_t)address);
}

static inline struct thread_t *read_gs_base(void) {
    return (struct thread_t *)read_msr(0xc0000101);
}

// kernel gs base
static inline void write_kernel_gs_base(struct thread_t *address) {
    write_msr(0xc0000102, (uintptr_t)address);
}

static inline struct thread_t *read_kernel_gs_base(void) {
    return (struct thread_t *)read_msr(0xc0000102);
}

static inline int_status_t ints_fetch_disable(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq\n\t"
                     "popq %0\n\t"
                     "cli"
                     : "=r" (rflags) : : "memory");
    return (int_status_t)((rflags >> 9) & 1);
}

static inline void ints_status_restore(int_status_t state) {
    if (state)
        __asm__ ("sti");
}

static inline void ints_off(void) {
    __asm__ ("cli");
}

static inline void ints_on(void) {
    __asm__ ("sti");
}

void nmi_enable(void);
void nmi_disable(void);

void acquire_lock(k_spinlock_t *lock);
void release_lock(k_spinlock_t *lock);
bool acquire_lock_timeout(k_spinlock_t *lock, size_t millis);