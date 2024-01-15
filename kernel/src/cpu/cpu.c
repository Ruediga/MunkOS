#include "cpu/cpu.h"

// https://gcc.gnu.org/onlinedocs/gcc-4.5.3/gcc/Atomic-Builtins.html

void acquire_lock(k_spinlock *lock) {
    while (!__sync_bool_compare_and_swap(&lock->lock, 0, 1)) {
        __asm__ ("pause");
    }
}

void release_lock(k_spinlock *lock) {
    //__atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}