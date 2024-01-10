#include "apic/apic.h"

#include "cpu/cpu.h"

// Specs: https://pdos.csail.mit.edu/6.828/2018/readings/ia32/ioapic.pdf
// https://wiki.osdev.org/IOAPIC

void initApic(void)
{
    // disable legacy PIC
    outb(PIC1_DATA, 0xff);
    outb(PIC2_DATA, 0xff);
}