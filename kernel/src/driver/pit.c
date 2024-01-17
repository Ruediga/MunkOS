#include "pit.h"
#include "macros.h"
#include "io.h"
#include "smp.h"
#include "apic.h"

/*
 * PIT (https://wiki.osdev.org/PIT)
 *
 * Oscillator runs at ~ 1.193182 MHz
 *
 * I/O port     Usage
 * 0x40         Channel 0 data port (RW) // IRQ 0
 * 0x42         Channel 2 data port (RW) // PC speaker
 * 0x43         Mode/Command register (WO)
 * 
 * Bits         Usage
 * 6 and 7      Select channel :
 *                  0 0 = Channel 0
 *                  1 0 = Channel 2
 *                  1 1 = Read-back command
 * 4 and 5      Access mode :
 *                  0 0 = Latch count value command
 *                  0 1 = Access mode: lobyte only
 *                  1 0 = Access mode: hibyte only
 *                  1 1 = Access mode: lobyte/hibyte
 * 1 to 3       Operating mode :
 *                  0 0 0 = Mode 0 (interrupt on terminal count)
 *                  0 0 1 = Mode 1 (hardware re-triggerable one-shot)
 *                  0 1 0 = Mode 2 (rate generator)
 *                          - set reload register
 *                          - 
 *                  0 1 1 = Mode 3 (square wave generator)
 *                  1 0 0 = Mode 4 (software triggered strobe)
 *                  1 0 1 = Mode 5 (hardware triggered strobe)
 *                  1 1 0 = Mode 2
 *                  1 1 1 = Mode 3
 * 0            BCD/Binary mode: 0 = 16-bit binary
 * 
 * - Every write to the mode/command register resets all
 *     internal logic in the selected PIT channel.
 * - New reload value can be written to a channel's data port at any time,
 *     the operating mode determines the exact effect that this will have. 
 * - Current counter value: either decremented (-1)
 *     or reset to reload value on falling edge of input signal. 
 * - In modes where the current count is decremented when it is reloaded,
 *     current count is not decremented on the same input clock pulse as
 *     reload – it starts decrementing on the next input clock pulse. 
*/
// channel 0 mode 2, rate = Hz
void pit_rate_init(uint16_t value)
{
    outb(0x43, 0b00110100); // command register
    outb(0x40, (uint8_t)value); // low first
    outb(0x40, (uint8_t)(value >> 8)); // high after
}

uint16_t pit_read_current(void)
{
    // counter latch command on channel 0 (bits [7:6])
    outb(0x43, 0b00000000);
    uint16_t count = (uint16_t)inb(0x40);
    return count | ((uint16_t)inb(0x40) << 8);
}

void pit_reset_to_default(void)
{
    // divisor = frequency / rate
    uint16_t reload_value = (uint16_t)DIV_ROUNDUP(PIT_OSCILLATOR_FREQUENCY, PIT_INT_FREQUENCY);
    pit_rate_init(reload_value);
}

#include "interrupt.h"
#include "kprintf.h"
static void pit_handler(INT_REG_INFO *regs)
{
    (void)regs;
    // do something
    lapic_send_eoi_signal();
}

void init_pit(void)
{
    pit_reset_to_default();

    interrupts_register_vector(0x70, (uintptr_t)pit_handler);
    ioapic_redirect_irq(0, 0x70, smp_request.response->bsp_lapic_id);
}