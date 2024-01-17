#pragma once

#include <stddef.h>
#include <stdint.h>

#include "kprintf.h"
#include "acpi.h"

#define DEADLOCK_MAX_ITERATIONS 100

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

typedef struct {
    // 0 = free, 1 = locked
    //_Atomic(uintptr_t) lock;
    size_t lock;
} k_spinlock;

void acquire_lock(k_spinlock *lock);
void release_lock(k_spinlock *lock);