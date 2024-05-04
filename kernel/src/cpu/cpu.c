#include "cpu.h"
#include "kprintf.h"
#include "acpi.h"
#include "interrupt.h"
#include "io.h"
#include "time.h"
#include "cpu_id.h"

// https://gcc.gnu.org/onlinedocs/gcc-4.5.3/gcc/Atomic-Builtins.html

void spin_lock(k_spinlock_t *lock) {
    size_t c = 0;
    while (!__sync_bool_compare_and_swap(&lock->lock, 0, 1)) {
        arch_spin_hint();
        if (++c > 1000000000ul) kpanic(0, NULL, "DEADLOCK\n");
    }
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

void spin_unlock(k_spinlock_t *lock) {
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}

inline void nmi_enable(void) {
    uint8_t in = inb(0x70);
    outb(0x70, in & 0x7F);
    inb(0x71);
}

inline void nmi_disable(void) {
    uint8_t in = inb(0x70);
    outb(0x70, in | 0x80);
    inb(0x71);
}