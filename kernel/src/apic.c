#include "kprintf.h"
#include "macros.h"
#include "kprintf.h"
#include "pmm.h"
#include "vmm.h"
#include "cpu.h"
#include "apic.h"
#include "acpi.h"
#include "io.h"
#include "smp.h"
#include "pit.h"

static uint64_t calibration_probe_count, calibration_timer_start, calibration_timer_end;

// ioapic
// ============================================================================
// map from ISR[32 - 47]

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

void ioapic_set_irq(uint32_t irq, uint32_t vector, uint32_t lapic_id, bool unset)
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
        kpanic(NULL, "IRQL %u isn't mapped to any IOAPIC!\n", (uint64_t)irq);
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

    if (unset) {
        entry_low = 0;
        entry_high = 0;
    }
    uint32_t table_index = (irq - ioapic->global_system_interrupt_base) * 2;
    ioapic_write(ioapic, 0x10 + table_index, entry_low);
    ioapic_write(ioapic, 0x10 + table_index + 1, entry_high);
}

// route irq to vector on lapic
void ioapic_redirect_irq(uint32_t irq, uint32_t vector, uint32_t lapic_id)
{
    ioapic_set_irq(irq, vector, lapic_id, 0);
}

void ioapic_remove_irq(uint32_t irq, uint32_t lapic_id)
{
    ioapic_set_irq(irq, 0, lapic_id, 1);
}

// lapic
// ============================================================================
// sdm vol3 ch 11.4
// 11.5: LVTs (Local Vector Tables)

// this gets mapped and incremented by the hhdm offset in the vmm!
uintptr_t lapic_address = 0xFEE00000;

inline uint32_t lapic_read(uint32_t reg)
{
    return *((volatile uint32_t *)(lapic_address + reg));
}

inline void lapic_write(uint32_t reg, uint32_t val)
{
    *((volatile uint32_t *)(lapic_address + reg)) = val;
}

void ipi_handler(cpu_ctx_t *regs)
{
    (void)regs;
    __asm__ ("cli");

    struct cpu *this_cpu = get_this_cpu();
    kprintf(", %u", this_cpu->id);

    __asm__ ("hlt");
}

size_t lapic_vectors_are_registered = 0;
void init_lapic(void)
{
    // check if lapics are where they're supposed to be
    if ((read_msr(0x1b) & 0xfffff000) != lapic_address - hhdm->offset) {
        kpanic(NULL, "LAPIC PA doesn't match\n");
    }

    if (!lapic_vectors_are_registered) {
        lapic_vectors_are_registered = 1;
        interrupts_register_vector(INT_VEC_LAPIC_TIMER, (uintptr_t)lapic_timer_handler);
        interrupts_register_vector(INT_VEC_LAPIC_IPI, (uintptr_t)ipi_handler);
        interrupts_register_vector(INT_VEC_SPURIOUS, (uintptr_t)default_interrupt_handler);
    }
    // enable APIC (1 << 8), spurious vector = 0xFF
    lapic_write(LAPIC_SPURIOUS_INT_VEC_REG, lapic_read(LAPIC_SPURIOUS_INT_VEC_REG) | 0x100 | 0xFF);

    calibrate_lapic_timer();
}

inline void lapic_send_eoi_signal(void)
{
    // non zero value may cause #GP
    lapic_write(LAPIC_EOI_REG, 0x00);
}

static void calibrate_lapic_timer_pit_callback(cpu_ctx_t *regs)
{
    (void)regs;
    uint32_t lapic_tmr_count = lapic_read(LAPIC_TIMER_CURRENT_COUNT_REG);

	calibration_probe_count++;

	if (calibration_probe_count == 1) {
        // start
		calibration_timer_start = lapic_tmr_count;
	}
	else if (calibration_probe_count == LAPIC_TIMER_CALIBRATION_PROBES) {
        // end
		calibration_timer_end = lapic_tmr_count;
	}
    lapic_send_eoi_signal();
}

/*
 * LAPIC_TIMER_INITIAL_COUNT_REG:
 *    - 0 stops it,
 *
*/
void calibrate_lapic_timer(void)
{
    lapic_write(LAPIC_TIMER_CURRENT_COUNT_REG, 0);

    // div: 1
    uint32_t reg_old = lapic_read(LAPIC_TIMER_DIV_CONFIG_REG);
    lapic_write(LAPIC_TIMER_DIV_CONFIG_REG, reg_old | 0b1011);

    // mask lapic timer interrupt vector
    reg_old = lapic_read(LAPIC_LVT_TIMER_REG);
    lapic_write(LAPIC_LVT_TIMER_REG, LAPIC_TIMER_LVTTR_MASKED);

    // start timer
    lapic_write(LAPIC_TIMER_INITIAL_COUNT_REG, 0xFFFFFFFF);

    calibration_probe_count = 0;

    pit_rate_init(LAPIC_TIMER_CALIBRATION_FREQ);
    ioapic_redirect_irq(0, INT_VEC_GENERAL_PURPOSE, get_this_cpu()->lapic_id);
    interrupts_register_vector(INT_VEC_GENERAL_PURPOSE, (uintptr_t)calibrate_lapic_timer_pit_callback);
    __asm__ ("sti");

    while (calibration_probe_count < LAPIC_TIMER_CALIBRATION_PROBES) {
        __asm__ ("pause");
    }

    __asm__ ("cli");

    ioapic_remove_irq(0, get_this_cpu()->lapic_id);
    interrupts_erase_vector(INT_VEC_GENERAL_PURPOSE);

    // calculate apic bus frequency
    uint64_t timer_delta = calibration_timer_start - calibration_timer_end;

    get_this_cpu()->lapic_clock_frequency = (timer_delta / LAPIC_TIMER_CALIBRATION_PROBES - 1) * LAPIC_TIMER_CALIBRATION_FREQ;
}

void lapic_timer_handler(cpu_ctx_t *regs)
{
    (void)regs;
struct cpu *this_cpu = get_this_cpu();
    kprintf("oneshot signal at cpu %lu\n", this_cpu->id);
    (void)this_cpu;

    lapic_send_eoi_signal();
}

void lapic_timer_periodic(size_t vector, size_t freq)
{
    struct cpu *this_cpu = get_this_cpu();
    uint32_t count_per_tick = this_cpu->lapic_clock_frequency / freq;

    // unmask
    lapic_write(LAPIC_TIMER_DIV_CONFIG_REG, 0b1011);
	lapic_write(LAPIC_TIMER_INITIAL_COUNT_REG, count_per_tick);
	lapic_write(LAPIC_LVT_TIMER_REG, vector | LAPIC_TIMER_LVTTR_PERIODIC);
}

// send an interrupt in n us (max 4000000)
void lapic_timer_oneshot_us(size_t vector, size_t us)
{
    struct cpu *this_cpu = get_this_cpu();
    uint32_t ticks_per_us = this_cpu->lapic_clock_frequency / 1000000ul;

    lapic_write(LAPIC_TIMER_DIV_CONFIG_REG, 0b1011);
	lapic_write(LAPIC_TIMER_INITIAL_COUNT_REG, ticks_per_us * us);
	lapic_write(LAPIC_LVT_TIMER_REG, vector | LAPIC_TIMER_LVTTR_ONESHOT);
}

// send an interrupt in n ms (max: 500000)
void lapic_timer_oneshot_ms(size_t vector, size_t ms)
{
    struct cpu *this_cpu = get_this_cpu();
    uint32_t ticks_per_ms = this_cpu->lapic_clock_frequency / 1000ul / 128ul;

    lapic_write(LAPIC_TIMER_DIV_CONFIG_REG, 0b1010);
	lapic_write(LAPIC_TIMER_INITIAL_COUNT_REG, ticks_per_ms * ms);
	lapic_write(LAPIC_LVT_TIMER_REG, vector | LAPIC_TIMER_LVTTR_ONESHOT);
}

void lapic_send_ipi(uint32_t lapic_id, uint32_t vector, enum LAPIC_ICR_DEST dest)
{
    uint32_t icr_low = lapic_read(LAPIC_ICR_REG_LOW), icr_high;

    while (icr_low & (LAPIC_ICR_PENDING << 12))
        __asm__ ("pause");

    icr_low = lapic_read(LAPIC_ICR_REG_LOW) & 0xFFF32000; // clear everything
    icr_high = lapic_read(LAPIC_ICR_REG_HIGH) & 0x00FFFFFF; // clear del field

    switch (dest)
    {
    case ICR_DEST_ALL:
        lapic_write(LAPIC_ICR_REG_HIGH, icr_high);
        lapic_write(LAPIC_ICR_REG_LOW, icr_low | (dest << 18) | vector);
        break;
    case ICR_DEST_OTHERS:
        lapic_write(LAPIC_ICR_REG_HIGH, icr_high);
        lapic_write(LAPIC_ICR_REG_LOW, icr_low | (dest << 18) | vector);
        break;
    case ICR_DEST_SELF:
        lapic_write(LAPIC_ICR_REG_HIGH, icr_high);
        lapic_write(LAPIC_ICR_REG_LOW, icr_low | (dest << 18) | vector);
        break;
    case ICR_DEST_FIELD:
        lapic_write(LAPIC_ICR_REG_HIGH, icr_high | (lapic_id << 24));
        lapic_write(LAPIC_ICR_REG_LOW, icr_low | (dest << 18) | vector);
        break;
    default:
        kprintf("<lapic_send_ipi> Unknown Destination\n");
    }
}
