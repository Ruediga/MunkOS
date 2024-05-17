#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t int_status_t;

// spins
typedef struct {
    size_t lock;
    int_status_t old_state;
} k_spinlock_t;

// yields
typedef struct {
    size_t lock;
} k_mutex_t;

void preempt_disable(void);
void preempt_enable(void);
int_status_t preempt_fetch();
int_status_t preempt_fetch_disable(void);
void preempt_restore(int_status_t state);

void spin_lock(k_spinlock_t *lock);
void spin_unlock(k_spinlock_t *lock);
void spin_lock_global(k_spinlock_t *lock);
void spin_unlock_global(k_spinlock_t *lock);
bool spin_lock_timeout(k_spinlock_t *lock, size_t millis);

void mutex_lock(k_mutex_t *mutex);
bool mutex_lock_timeout(k_mutex_t *mutex, size_t millis);
void mutex_unlock(k_mutex_t *mutex);