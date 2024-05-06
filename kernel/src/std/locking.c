#include "locking.h"
#include "time.h"
#include "scheduler.h"
#include "compiler.h"

comp_no_asan void spin_lock(k_spinlock_t *lock) {
    size_t c = 0;
    while (!__sync_bool_compare_and_swap(&lock->lock, 0, 1)) {
        arch_spin_hint();
        if (++c > 10000000ul) kpanic(0, NULL, "DEADLOCK\n");
    }
}

// return 0 on timeout
comp_no_asan bool spin_lock_timeout(k_spinlock_t *lock, size_t millis) {
    // 1000 hz
    size_t end = system_ticks + millis;
    while (!__sync_bool_compare_and_swap(&lock->lock, 0, 1)) {
        arch_spin_hint();
        if (system_ticks > end) return false;
    }
    return true;
}

comp_no_asan void spin_unlock(k_spinlock_t *lock) {
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}



void mutex_lock(k_mutex_t *mutex) {
    while (!__sync_bool_compare_and_swap(&mutex->lock, 0, 1)) {
        scheduler_yield();
    }
}

// return 0 on timeout
bool mutex_lock_timeout(k_mutex_t *mutex, size_t millis) {
    // 1000 hz
    size_t end = system_ticks + millis;
    while (!__sync_bool_compare_and_swap(&mutex->lock, 0, 1)) {
        scheduler_yield();
        if (system_ticks > end) return false;
    }
    return true;
}

void mutex_unlock(k_mutex_t *mutex) {
    __atomic_store_n(&mutex->lock, 0, __ATOMIC_RELEASE);
}
