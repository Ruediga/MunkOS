#include "apic/apic.h"

#include "cpu/cpu.h"

void initApic(void)
{
    // disable legacy PIC
    outb(PIC1_DATA, 0xff);
    outb(PIC2_DATA, 0xff);

    
}