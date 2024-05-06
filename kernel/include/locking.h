#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cpu.h"

// spins
typedef struct {
    size_t lock;
} k_spinlock_t;

// yields
typedef struct {
    size_t lock;
} k_mutex_t;

void spin_lock(k_spinlock_t *lock);
void spin_unlock(k_spinlock_t *lock);
bool spin_lock_timeout(k_spinlock_t *lock, size_t millis);