#include "cpu.h"
#include "kprintf.h"
#include "acpi.h"
#include "interrupt.h"

// https://gcc.gnu.org/onlinedocs/gcc-4.5.3/gcc/Atomic-Builtins.html

void acquire_lock(k_spinlock_t *lock) {
    size_t c = 0;
    while (!__sync_bool_compare_and_swap(&lock->lock, 0, 1)) {
        __asm__ ("pause");
        if (++c > 10000000ul) kpanic(NULL, "DEADLOCK\n");
    }
}

void release_lock(k_spinlock_t *lock) {
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}