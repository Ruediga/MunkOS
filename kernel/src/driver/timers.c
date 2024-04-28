#include "macros.h"
#include "io.h"
#include "smp.h"
#include "apic.h"
#include "time.h"
#include "cpu.h"
#include "interrupt.h"
#include "kprintf.h"

#define CMOS_SELECT ((uint8_t)0x70)
#define CMOS_DATA ((uint8_t)0x71)

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
void pit_rate_init(size_t freq)
{
    uint16_t reload_value = (uint16_t)DIV_ROUNDUP(PIT_OSCILLATOR_FREQUENCY, freq);
    outb(0x43, 0b00110100); // command register
    outb(0x40, (uint8_t)reload_value); // low first
    outb(0x40, (uint8_t)(reload_value >> 8)); // high after
}

uint16_t pit_read_current(void)
{
    // counter latch command on channel 0 (bits [7:6])
    outb(0x43, 0b00000000);
    uint16_t count = (uint16_t)inb(0x40);
    return count | ((uint16_t)inb(0x40) << 8);
}

volatile size_t pit_ticks = 0;
static void pit_handler(cpu_ctx_t *regs)
{
    (void)regs;
    pit_ticks++;
    lapic_send_eoi_signal();
}

void init_pit(void)
{
    pit_rate_init(PIT_INT_FREQUENCY);

    interrupts_register_vector(INT_VEC_PIT, (uintptr_t)pit_handler);
    ioapic_redirect_irq(0, INT_VEC_PIT, smp_request.response->bsp_lapic_id);
}

inline void wrt_cmos_cfg_space(uint8_t reg, uint8_t value)
{
    switch (reg) {
        case 0x0A: case 0x0B: case 0x0C: case 0x0D: break;
        default: kpanic(0, NULL, "invalid cmos cfg space register selector");
    }

    int_status_t state = ints_fetch_disable();
    outb(CMOS_SELECT, 0x80 | reg);     // select / disable nmi
    __asm__ ("pause");
    outb(CMOS_DATA, value);          // write
    nmi_enable();
    ints_status_restore(state);
}
 
// 1024hz
inline void rtc_set_periodic(uint8_t enable)
{
    int_status_t state = ints_fetch_disable();
    outb(CMOS_SELECT, 0x8B);           // select / disable nmi
    uint8_t prev = inb(CMOS_DATA);   // save
    outb(CMOS_SELECT, 0x8B);           // restore (read resets selector to reg D)
    if (enable)
        outb(CMOS_DATA, 0x40 | prev);    // set reg B bit 6
    else
        outb(CMOS_DATA, ~0x40 & prev);    // unset reg B bit 6
    nmi_enable();
    ints_status_restore(state);
}

// 0 (off) || 2 (8khz) - 15(2hz) (default 6): freq = 32768 >> (rate - 1)
inline void rtc_set_rate(uint8_t rate)
{
    if ((rate < 3 && rate) || rate > 15)
        kpanic(0, NULL, "invalid rtc rate");

    int_status_t state = ints_fetch_disable();
    outb(CMOS_SELECT, 0x8A);		    // select A, disable nmi
    uint8_t prev = inb(CMOS_DATA);	    // save
    outb(CMOS_SELECT, 0x8A);		    // restore (read resets selector to reg D)
    outb(CMOS_DATA, (prev & 0xF0) | rate);   // rate low 4 bits
    nmi_enable();
    ints_status_restore(state);
}

// types: alarm, update ended, periodic
// flush this or interrupt won't happen again
inline uint8_t rtc_rd_int_type(void)
{
    outb(CMOS_SELECT, 0x0C);   // select reg C
    return inb(CMOS_DATA);
}

volatile size_t rtc_ticks = 0;
static void rtc_handler(cpu_ctx_t *regs)
{
    (void)regs;
    rtc_ticks++;
    //kprintf("%lu\n", rtc_ticks);
    rtc_rd_int_type();
    lapic_send_eoi_signal();
}

void rtc_init(void)
{
    rtc_set_periodic(1);

    interrupts_register_vector(INT_VEC_RTC, (uintptr_t)rtc_handler);
    ioapic_redirect_irq(8, INT_VEC_RTC, smp_request.response->bsp_lapic_id);
}



/*
Accessing CMOS is extremely simple, but you always need to take into account how you want 
to handle NMI. You "select" a CMOS register (for reading or writing) by sending the register
 number to IO Port CMOS_SELECT. Since the 0x80 bit of Port CMOS_SELECT controls NMI, you always end up 
 setting that, too. So your CMOS controller always needs to know whether your OS wants NMI
  to be enabled or not. Selecting a CMOS register is done as follows:

    outb (CMOS_SELECT, (NMI_disable_bit << 7) | (selected CMOS register number)); 

Once a register is selected, you either read the value of that register on 
Port CMOS_DATA (with inb or an equivalent function), or you write a new value to that register 
-- also on Port CMOS_DATA (with outb, for example):

    val_8bit = inb (CMOS_DATA); 

Note1: Reading or writing Port CMOS_DATA seems to default the "selected register" back to 0xD.
So you need to reselect the register every single time you want to access a CMOS register.

Note2: It is probably a good idea to have a reasonable delay after selecting a CMOS 
register on Port CMOS_SELECT, before reading/writing the value on Port CMOS_DATA.

There are many CMOS registers in various locations, used by various ancient BIOSes, 
to store a "hard disk type" or other hard disk information.
 Any such information is strictly for use on obsolete CHS-based disk drives. 
 Better information can always be obtained via BIOS function INT13h AH=8, 
 or by sending an ATA 'Identify' command to the disk in ATA PIO Mode.
The Real-Time Clock

The RTC keeps track of the date and time, even when the computer's power is off. 
The only other way that a computer used to be able to do this was to ask a human 
for the date/time on every bootup. Now, if the computer has an internet connection, 
an OS has another (arguably better) way to get the same information.

The RTC also can generate clock ticks on IRQ8 (similarly to what the PIT does on IRQ0). 
The highest feasible clock frequency is 8KHz. Using the RTC clock this way may actually 
generate more stable clock pulses than the PIT can generate. 
It also frees up the PIT for timing events that really need near-microsecond accuracy.
 Additionally, the RTC can generate an IRQ8 at a particular time of day. 
 See the RTC article for more detailed information about using RTC interrupts.
Getting Current Date and Time from RTC

To get each of the following date/time values from the RTC, 
you should first ensure that you won't be effected by an update (see below). 
Then select the associated "CMOS register" in the usual way, 
and read the value from Port CMOS_DATA.

Register  Contents            Range
 0x00      Seconds             0–59
 0x02      Minutes             0–59
 0x04      Hours               0–23 in 24-hour mode, 
                               1–12 in 12-hour mode, highest bit set if pm
 0x06      Weekday             1–7, Sunday = 1
 0x07      Day of Month        1–31
 0x08      Month               1–12
 0x09      Year                0–99
 0x32      Century (maybe)     19–20?
 0x0A      Status Register A
 0x0B      Status Register B

The correct way to determine the current day of the week is to calculate it 
from the date (see the article on Wikipedia for details of this calculation).
RTC Update In Progress

When the chip updates the time and date (once per second) it increases "seconds" and 
checks if it rolled over. If "seconds" did roll over it increases "minutes" and 
checks if that rolled over. This can continue through all the time and date 
registers (e.g. all the way up to "if year rolled over, increase century"). 
However, the RTC circuitry is typically relatively slow. 
This means that it's entirely possible to read the time and date while an update 
is in progress and get dodgy/inconsistent values (for example, at 9:00 o'clock 
you might read 8:59, or 8:60, or 8:00, or 9:00).

To help guard against this problem the RTC has an "Update in progress" flag 
(bit 7 of Status Register A). To read the time and date properly you have to wait
 until the "Update in progress" flag goes from "set" to "clear". 
 This is not the same as checking that the "Update in progress" flag is clear.

For example, if code does "while(update_in_progress_flag != clear)" and then starts 
reading all the time and date registers, then the update could begin immediately after 
the "Update in progress" flag was checked and the code could still read dodgy/inconsistent 
values. To avoid this, code should wait until the flag becomes set and then wait until the 
flag becomes clear. That way there's almost 1 second of time to read all of the registers 
correctly.

Unfortunately, doing it correctly (waiting until the "Update in progress" flag becomes 
set and then waiting until it becomes clear) is very slow - it may take an entire second 
of waiting/polling before you can read the registers. There are 2 alternatives.

The first alternative is to rely on the "update interrupt". 
When the RTC finishes an update it generates an "update interrupt" (if it's enabled), 
and the IRQ handler can safely read the time and date registers without worrying 
about the update at all (and without checking the "Update in progress" flag); 
as long as the IRQ handler doesn't take almost a full second to do it. 
In this case you're not wasting up to 1 second of CPU time waiting/polling, 
but it may still take a full second before the time and date has been read. 
Despite this it can be a useful technique during OS boot - e.g. setup the 
"update interrupt" and its IRQ handler as early as you can and then do other 
things (e.g. loading files from disk), in the hope that the IRQ occurs 
before you need the time and date.

The second alternative is to be prepared for dodgy/inconsistent values and cope with 
them if they occur. To do this, make sure the "Update in progress" flag is clear 
(e.g. "while(update_in_progress_flag != clear)") then read all the time and date 
registers; then make sure the "Update in progress" flag is clear again 
(e.g. "while(update_in_progress_flag != clear)") and read all the time and date 
registers again. If the values that were read the first time are the same as the 
value that were read the second time then the values must be correct. 
If any of the values are different you need to do it again, and keep doing it 
again until the newest values are the same as the previous values.
Format of Bytes

There are 4 formats possible for the date/time RTC bytes:

    Binary or BCD Mode
    Hours in 12 hour format or 24 hour format 

The format is controlled by Status Register B. 
On some CMOS/RTC chips, the format bits in Status Reg B cannot be changed. 
So your code needs to be able to handle all four possibilities, 
and it should not try to modify Status Register B's settings. 
So you always need to read Status Register B first, to find out what format your 
date/time bytes will arrive in.

    Status Register B, Bit 1 (value = 2): Enables 24 hour format if set
    Status Register B, Bit 2 (value = 4): Enables Binary mode if set 

Binary mode is exactly what you would expect the value to be. 
If the time is 1:59:48 AM, then the value of hours would be 1, 
minutes would be 59 = 0x3b, and seconds would be 48 = 0x30.

In BCD mode, each of the two hex nibbles of the byte is modified to "display" 
a decimal number. So 1:59:48 has hours = 1, minutes = 0x59 = 89, seconds = 0x48 = 72. 
To convert BCD back into a "good" binary value, 
use: binary = ((bcd / 16) * 10) + (bcd & 0xf) 
[Optimised: binary = ( (bcd & 0xF0) >> 1) + ( (bcd & 0xF0) >> 3) + (bcd & 0xf)].

24 hour time is exactly what you would expect. Hour 0 is midnight to 1am, hour 23 is 11pm.

12 hour time is annoying to convert back to 24 hour time. If the hour is pm, 
then the 0x80 bit is set on the hour byte. So you need to mask that off. 
(This is true for both binary and BCD modes.) Then, midnight is 12, 1am is 1, etc. 
Note that carefully: midnight is not 0 -- it is 12 -- this needs to be handled as 
a special case in the calculation from 12 hour format to 24 hour format
(by setting 12 back to 0)!

For the weekday format: Sunday = 1, Saturday = 7.
Interpreting RTC Values

On the surface, these values from the RTC seem extremely obvious. 
 main difficulty comes in deciding which timezone the values represent. 
 The two possibilities are usually UTC, or the system's timezone, 
 including Daylight Savings. See the Time And Date article for a much more 
 complete discussion of how to handle this issue.
Examples*/

//static inline 

rtc_time_ctx_t rd_rtc(void)
{
    rtc_time_ctx_t c = {};
    return c;
}

/*Reading All RTC Time and Date Registers

#define CURRENT_YEAR        2023                            // Change this each year!
 
int century_register = 0x00;                                // Set by ACPI table parsing code if possible
 
enum {
      cmos_address = CMOS_SELECT,
      cmos_data    = CMOS_DATA
};
 
int get_update_in_progress_flag() {
      out_byte(cmos_address, 0x0A);
      return (in_byte(cmos_data) & 0x80);
}
 
unsigned char get_RTC_register(int reg) {
      out_byte(cmos_address, reg);
      return in_byte(cmos_data);
}
 
void read_rtc() {
      unsigned char century;
      unsigned char last_second;
      unsigned char last_minute;
      unsigned char last_hour;
      unsigned char last_day;
      unsigned char last_month;
      unsigned char last_year;
      unsigned char last_century;
      unsigned char registerB;
 
      // Note: This uses the "read registers until you get the same values twice in a row" technique
      //       to avoid getting dodgy/inconsistent values due to RTC updates
 
      while (get_update_in_progress_flag());                // Make sure an update isn't in progress
      second = get_RTC_register(0x00);
      minute = get_RTC_register(0x02);
      hour = get_RTC_register(0x04);
      day = get_RTC_register(0x07);
      month = get_RTC_register(0x08);
      year = get_RTC_register(0x09);
      if(century_register != 0) {
            century = get_RTC_register(century_register);
      }
 
      do {
            last_second = second;
            last_minute = minute;
            last_hour = hour;
            last_day = day;
            last_month = month;
            last_year = year;
            last_century = century;
 
            while (get_update_in_progress_flag());           // Make sure an update isn't in progress
            second = get_RTC_register(0x00);
            minute = get_RTC_register(0x02);
            hour = get_RTC_register(0x04);
            day = get_RTC_register(0x07);
            month = get_RTC_register(0x08);
            year = get_RTC_register(0x09);
            if(century_register != 0) {
                  century = get_RTC_register(century_register);
            }
      } while( (last_second != second) || (last_minute != minute) || (last_hour != hour) ||
               (last_day != day) || (last_month != month) || (last_year != year) ||
               (last_century != century) );
 
      registerB = get_RTC_register(0x0B);
 
      // Convert BCD to binary values if necessary
 
      if (!(registerB & 0x04)) {
            second = (second & 0x0F) + ((second / 16) * 10);
            minute = (minute & 0x0F) + ((minute / 16) * 10);
            hour = ( (hour & 0x0F) + (((hour & CMOS_SELECT) / 16) * 10) ) | (hour & 0x80);
            day = (day & 0x0F) + ((day / 16) * 10);
            month = (month & 0x0F) + ((month / 16) * 10);
            year = (year & 0x0F) + ((year / 16) * 10);
            if(century_register != 0) {
                  century = (century & 0x0F) + ((century / 16) * 10);
            }
      }
 
      // Convert 12 hour clock to 24 hour clock if necessary
 
      if (!(registerB & 0x02) && (hour & 0x80)) {
            hour = ((hour & 0x7F) + 12) % 24;
      }
 
      // Calculate the full (4-digit) year
 
      if(century_register != 0) {
            year += century * 100;
      } else {
            year += (CURRENT_YEAR / 100) * 100;
            if(year < CURRENT_YEAR) year += 100;
      }
}*/