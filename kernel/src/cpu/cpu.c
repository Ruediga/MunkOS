#include "cpu.h"

#include "kprintf.h"
#include "acpi.h"
#include "interrupt.h"

// https://gcc.gnu.org/onlinedocs/gcc-4.5.3/gcc/Atomic-Builtins.html

void acquire_lock(k_spinlock_t *lock) {
    volatile size_t c = 0;
    while (!__sync_bool_compare_and_swap(&lock->lock, 0, 1) && c < 10000000) {
        __asm__ ("pause");
        c++;
    }
    if (c >= 10000000) kpanic(NULL, "DEADLOCK\n");

    if (interrupts_enabled()) lock->dumb_idea = 1;
    __asm__ ("cli");
    return;
}

void release_lock(k_spinlock_t *lock) {
    if (lock->dumb_idea) {
        lock->dumb_idea = 0;
        __asm__ ("sti");
    }
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}