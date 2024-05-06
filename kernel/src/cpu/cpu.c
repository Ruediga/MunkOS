#include "cpu.h"
#include "kprintf.h"
#include "_acpi.h"
#include "interrupt.h"
#include "io.h"
#include "time.h"
#include "cpu_id.h"

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