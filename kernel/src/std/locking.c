#include "locking.h"
#include "cpu.h"
#include "interrupt.h"
#include "time.h"
#include "scheduler.h"

/* preempt_disable(): clear IF
 * preempt_enable(): set IF
 * preempt_fetch(): return IF
 * preempt_fetch_disable(): returns preempt_fetch() and calls preempt_disable()
 * preempt_restore(): set IF = old_status
 *
 * spin_(un)lock(): takes/releases lock
 * spin_(un)lock_global(): disables/restores preemption, takes/releases lock */

inline void preempt_disable(void) {
    __asm__ volatile ("cli" : : :);
}

inline void preempt_enable(void) {
    __asm__ volatile ("sti" : : :);
}

inline int_status_t preempt_fetch()
{
    uint64_t rflags;
    __asm__ volatile (
        "pushfq\n\r"
        "pop %0"
        : "=rm" (rflags)
        :
        : "memory"
    );
    return !!(rflags & (1 << 9));
}

inline int_status_t preempt_fetch_disable(void) {
    int_status_t old = preempt_fetch();
    preempt_disable();
    return old;
}

inline void preempt_restore(int_status_t state) {
    if (state)
        preempt_enable();
}

void spin_lock(k_spinlock_t *lock) {
    // could use cas here to store taking task ptr, for now tas does the job
    // only sets the first byte at ptr
    size_t c = 0;
    while (__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE)) {
        arch_spin_hint();
        if (++c > 10000000ul) kpanic(0, NULL, "DEADLOCK at %p\n", &lock->lock);
    }
}

void spin_unlock(k_spinlock_t *lock) {
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}

void spin_lock_global(k_spinlock_t *lock) {
    lock->old_state = preempt_fetch_disable();
    spin_lock(lock);
}

void spin_unlock_global(k_spinlock_t *lock) {
    spin_unlock(lock);

    preempt_restore(lock->old_state);
}

// return 0 on timeout
bool spin_lock_timeout(k_spinlock_t *lock, size_t millis) {
    // 1000 hz
    size_t end = system_ticks + millis;
    while (!__sync_bool_compare_and_swap(&lock->lock, 0, 1)) {
        arch_spin_hint();
        if (system_ticks > end) return false;
    }
    return true;
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
