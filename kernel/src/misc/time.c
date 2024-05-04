#include "time.h"
#include "interrupt.h"
#include "smp.h"
#include "apic.h"
#include "kevent.h"
#include "kprintf.h"

volatile size_t system_ticks;
volatile size_t unix_time;

// this would be fun to do tickless
void common_timer_handler(void)
{
    // we keep track of system time and timespans from here
    __atomic_add_fetch(&system_ticks, 1, __ATOMIC_SEQ_CST);
    if (system_ticks % 1000 == 0) {
        unix_time++;
    }
}

static void rtc_handler(cpu_ctx_t *regs)
{
    (void)regs;
    rtc_rd_int_type();

    lapic_send_eoi_signal();

    common_timer_handler();
}

// rtc: ticking timer
// lapic timer: preempt timer
// tsc: high precision timer
// hpet: -
// pit: -
void time_init(void)
{
    // use the rtc interrupt for the common system timer.
    // we let it run at 1000hz, and all system timers are updated during its isrs.
    // we also keep a unix timestamp that gets incremented from there.

    rtc_time_ctx_t ctx = rd_rtc();
    unix_time = rtc_time2unix_stamp(ctx);

    // the order set_periodic() - set_rate() - register- and redirect_irq()
    // does not work on real hardware!!! most likely what happens is that the first
    // interrupt fires while the irq isn't yet redirected, and in turn the
    // cmos int type register doesn't get cleared, which stops all adherent
    // interrupts from firing. To circumvent this issue, register the handlers
    // first, and only then enable and configure the timer.

    interrupts_register_vector(INT_VEC_RTC, (uintptr_t)rtc_handler);
    // run system timer on the bsp
    ioapic_redirect_irq(8, INT_VEC_RTC, smp_request.response->bsp_lapic_id);

    rtc_set_periodic(1);
    // rate 6 = 1024hz
    rtc_set_rate(6);
}

// a timer triggers an event on running out. an event can have event listeners (a thread
// waiting for the timer to expire). upon expiration, the timer gets deleted, and the event
// wakes all sleeping threads.
// sleep event with timer:
// 0.) yield into sleeping list
// 1.) init event to reschedule its subscribers
// 2.) subscribe to event
// 2.) register timer handler for event
// 3.) timer expires
// 4.) event gets triggered and added to even worker thread queue (high prio)
// 5.) the worker thread executes the event (wakes all subscribers)

// minimum precision 1ms, for higher precision timers use something else
// [TODO] how add event to this?
void register_system_timer(struct ktimer *tmr, size_t ms)
{
    (void)ms;
    (void)tmr;
}
