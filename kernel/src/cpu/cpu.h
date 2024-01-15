#pragma once

#include <stddef.h>
#include <stdint.h>

#include "std/kprintf.h"

#define DEADLOCK_MAX_ITERATIONS 100

inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

typedef struct {
    // 0 = free, 1 = locked
    //_Atomic(uintptr_t) lock;
    size_t lock;
} k_spinlock;

void acquire_lock(k_spinlock *lock);
void release_lock(k_spinlock *lock);