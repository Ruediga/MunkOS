#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define lfence() {__asm__ volatile ("lfence");}
#define sfence() {__asm__ volatile ("sfence");}
#define mfence() {__asm__ volatile ("mfence");}

#define arch_spin_hint() __asm__ volatile ("pause")

struct task;

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
    size_t id;                      // core id
    uint32_t lapic_id;              // lapic id of the processor
    uint64_t lapic_clock_frequency;

    struct task_state_segment tss;
    struct task *idle_thread;
    struct task *curr_thread;
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

// fs base: leave free for userspace
static inline void write_fs_base(uintptr_t address) {
    write_msr(0xc0000100, address);
}

static inline uintptr_t read_fs_base(void) {
    return read_msr(0xc0000100);
}

// gs base: leave free for userspace
static inline void write_gs_base(uintptr_t val) {
    write_msr(0xc0000101, (uintptr_t)val);
}

static inline  uintptr_t read_gs_base(void) {
    return read_msr(0xc0000101);
}

// kernel gs base: keep cpu_local_t * to this cpu here
static inline void write_kernel_gs_base(cpu_local_t *address) {
    write_msr(0xc0000102, (uintptr_t)address);
}

static inline cpu_local_t *read_kernel_gs_base(void) {
    return (cpu_local_t *)read_msr(0xc0000102);
}

// tsc aux register (rdpid and rdtscp)
static inline void write_tsc_aux(uint64_t pid) {
    write_msr(0xC0000103, pid);
}

static inline uint64_t read_processor_id(void) {
    uint64_t pid;
    __asm__ volatile ("rdpid %0" : "=r" (pid));
    return pid;
}

void nmi_enable(void);
void nmi_disable(void);