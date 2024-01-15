#include "apic/ioapic.h"
#include "cpu/cpu.h"
#include "acpi/acpi.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "std/kprintf.h"
#include "std/macros.h"

// map from ISR[32 - 47]
#define PIC_MASTER_OFFSET 0x20
#define PIC_SLAVE_OFFSET 0x28

/*
 * -> Basics:
 * IRQs (Interrupt Requests):
 *  - sent by devices on some interrupt request line
 *  - received by IOA/x2A/PIC and redirected to LAPIC
 *
 * GSI (Global System Interrupts):
 *  - global irq "count"
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
 *  - up to 24 64 bit entrys in the IRT (Int Redirection Tables) (write to 2x as uint32_t)
 *  - programmable Registers (32 bit):
 *     - IOREGSEL (IO Register Select) (base + 0h):
 *        - offset 0, length 8 bit
 *        - which ioapic internal register to write to
 *     - IOWINREG (IO Window Register) (base + 10h):
 *        - r/w to/from the register set in IOREGSEL
 *  - ioapic registers:
 *     - IOAPICID (reg 0h):
 *        - [27:24] IOAPIC ID (from acpi) (RW)
 *     - IOAPICVER (reg 1h):
 *        - [23:16] maximum redirection entry index (entry_count - 1) (RO)
 *        - [7:0] APIC version. (RO)
 *     - IOAPICARB (reg 2h):
 *        - [27:24] apic arbitration ID (RW)
 *     - IOREDTBL (reg 10h + entry_index * 1h):
 *        - [63/59:56] LAPIC ID (use this) / processor smth
 *        - [16:11] flags
 *        - [10:8] Delivery mode (fixed (0) for now, edge / high)
 *        - [7:0] Interrupt Vector
 *  - Sends APIC messages over the APIC bus (received by LAPICs)
 *  - selects IRQL in the IRT and based on that entry, sends interrupt
 *    with some priority to some CPU (LAPIC). Either high or edge triggered
*/
void init_ioapic(void)
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

    // should be 4KiB aligned, if not, good luck
    for (size_t i = 0; i < ioapic_count; i++) {
        vmm_map_single_page(&kernel_pmc, ALIGN_DOWN(((uintptr_t)ioapics[i].io_apic_address + hhdm->offset), PAGE_SIZE),
            ALIGN_DOWN((uintptr_t)ioapics[i].io_apic_address, PAGE_SIZE), PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
    }
}

void ioapic_write(struct acpi_ioapic *ioapic, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)ioapic->io_apic_address + hhdm->offset) = reg;
    *(volatile uint32_t *)((uintptr_t)ioapic->io_apic_address + hhdm->offset + 0x10) = val;
}

uint32_t ioapic_read(struct acpi_ioapic *ioapic, uint32_t reg)
{
    *(volatile uint32_t *)((uintptr_t)ioapic->io_apic_address + hhdm->offset) = reg;
    return *(volatile uint32_t *)((uintptr_t)ioapic->io_apic_address + hhdm->offset + 0x10);
}

// route irq to vector on lapic, note: make sure args don't contain clutter
void ioapic_redirect_irq(uint32_t irq, uint32_t vector, uint32_t lapic_id)
{
    uint16_t iso_flags = 0;
    for (size_t i = 0; i < iso_count; i++) {
        // check if motherboard maps "default" irqls differently
        if (isos[i].source_irq == irq) {
            irq = isos[i].global_system_interrupt;
            iso_flags = isos[i].flags;
            break;
        }
    }

    struct acpi_ioapic *ioapic = NULL;
    // find the corresponding ioapic for the irq
    for (size_t i = 0; i < ioapic_count; i++) {
        if (ioapics[i].global_system_interrupt_base <= irq
            && irq < ioapics[i].global_system_interrupt_base + ((ioapic_read(&(ioapics[i]), 0x1) & 0xFF0000) >> 16)) {
            ioapic = &(ioapics[i]);
        }
    }
    if (!ioapic) {
        kprintf("IRQL %u isn't mapped to any IOAPIC!\n", (uint64_t)irq);
        __asm__ volatile("cli\n hlt");
    }

    uint32_t entry_high = lapic_id << (56 - 32);
    uint32_t entry_low = vector;

    if (iso_flags & 0b11) {
        // polarity active low
        entry_low |= 1ul << 13;
    }
    if (iso_flags & 0b1100) {
        // level triggered
        entry_low |= 1ul << 15;
    }

    // entry |= 1ul << 16 // masked
    // entry |= 1ul << 14 // only for level triggered RO
    // entry |= 1ul << 11 // 0 = lapic id, 1 = logical mode
    // also stay with FIXED DELMOD

    // should work with multiple ioapics
    uint32_t table_index = (irq - ioapic->global_system_interrupt_base) * 2;
    ioapic_write(ioapic, 0x10 + table_index, entry_low);
    ioapic_write(ioapic, 0x10 + table_index + 1, entry_high);
}