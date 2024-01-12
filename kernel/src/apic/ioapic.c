#include "apic/ioapic.h"
#include "cpu/cpu.h"
#include "acpi/acpi.h"

#include <stdint.h>

// Specs: https://pdos.csail.mit.edu/6.828/2018/readings/ia32/ioapic.pdf
// https://wiki.osdev.org/IOAPIC

// map from ISR[32 - 47]
#define PIC_MASTER_OFFSET 0x20
#define PIC_SLAVE_OFFSET 0x28

/*static void io_apic_write(struct madt_io_apic *io_apic, uint32_t reg, uint32_t value) {
    uint64_t base = (uint64_t)io_apic->address + VMM_HIGHER_HALF;
    *(volatile uint32_t *)base = reg;
    *(volatile uint32_t *)(base + 16) = value;
}

static uint32_t io_apic_read(struct ioapic *_ioapic, uint32_t reg) {
    uint64_t base = (uint64_t)io_apic->address + VMM_HIGHER_HALF;
    *(volatile uint32_t *)base = reg;
    return *(volatile uint32_t *)(base + 16);
}*/

/*
 * -> Basics
 * IRQs (Interrupt Requests):
 *  - sent by devices on some interrupt request line
 *  - received by IOA/x2A/PIC and redirected to LAPIC
 *
 * GSI (Global System Interrupts):
 *  - IRQ mapped to GSI by Int Controller
 *
 * IV (Interrupt Vector):
 *  - 0 - 255, CPU, corresponds to a ISR (IDT)
 *
 * IPI (Interprocessor Interrupt):
 *  - Interrupts to another processors by LAPIC
 *
 *
 *
 * -> PIC:
 * https://pdos.csail.mit.edu/6.828/2005/readings/hardware/8259A.pdf
 * https://web.archive.org/web/20140628205356/www.acm.uiuc.edu/sigops/roll_your_own/i386/irq.html
 * https://wiki.osdev.org/PIC
 *
 * each master and slave have a command and data port
 * Master PIC:
 *  - command = 0x20
 *  - data = 0x21
 * Slave PIC:
 *  - command = 0xA0
 *  - data = 0xA1
 *
 * Talking to the PIC:
 *  - send Interrupt Control Word 1 (ICW1) to start a sequence
 *
 * Disabling:
 *  - mask interrupts
 *
 *
 *
 * -> IOAPIC:
 * https://pdos.csail.mit.edu/6.828/2018/readings/ia32/ioapic.pdf
 *
 *  - Figure 2. (page 4 ioapic specs)
 *  - up to 24 64 bit entrys in the IRT (Int Redirection Tables)
 *  - programmable Registers:
 *     - IOREGSEL (IO Register Select):
 *        - ?
 *     - IOWINREG (IO Window Register)
 *        - ?
 *  - Sends APIC messages over the APIC bus (received by LAPICs)
 *  - selects IRQL in the IRT and based on that entry, sends interrupt
 *    with some priority to some CPU (LAPIC). Either high or edge triggered
*/
void initApic(void)
{
    // ICW1 command:
    outb(0x20, 0b00010001); // INIT | SEND_ICW4
    outb(0xA0, 0b00010001); // INIT | SEND_ICW4

    // ICW2 data:
    outb(0x21, PIC_MASTER_OFFSET); // idt ISR offset
    outb(0xA1, PIC_SLAVE_OFFSET); // idt ISR offset

    // ICW3 data:
    outb(0x21, 0b00000100); // slave PIC at irq2
    outb(0xA1, 0b00000010); // slave PIC at ir2 of master

    // ICW2 data:
    outb(0x21, 0b00000101); // 8086_MODE | MASTER
    outb(0xA1, 0b00000001); // 8086_MODE

    // mask all interrupts
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    // [TODO] do some ioapic - lapic gsi redirection stuff
}