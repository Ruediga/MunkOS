#include "time.h"
#include "interrupt.h"
#include "smp.h"
#include "apic.h"
#include "kprintf.h"

#define TIMER_NODE_LEFT 1
#define TIMER_NODE_RIGHT 2

#define MIN_TIMER_HEAP_SIZE 64

volatile size_t system_ticks;
volatile size_t unix_time;

static struct ktimer_node **heap_array;
static int heap_capacity;
static int heap_size;
static k_spinlock_t heap_lock;

static inline void swap(int i, int j) {
    __asm__ __volatile__ (
        "xchgq %0, %1"
        : "+r" (heap_array[i]), "+r" (heap_array[j])
    );
}

static inline int get_child(int index, int dir) {
    return (index << 1) + dir;
}

static inline int get_parent(int index) {
    return (index - 1) >> 1;
}

static void min_heapify(int idx)
{
    for (;;) {
        int child_left = get_child(idx, TIMER_NODE_LEFT);
        int child_right = get_child(idx, TIMER_NODE_RIGHT);
        int right_idx = idx;

        if (child_left < heap_size && heap_array[child_left]->expiration_time < heap_array[idx]->expiration_time)
            right_idx = child_left;

        if (child_right < heap_size && heap_array[child_right]->expiration_time < heap_array[right_idx]->expiration_time)
            right_idx = child_right;

        if (right_idx == idx)
            break;

        swap(idx, right_idx);
        idx = right_idx;
    }
}

static void timer_insert(struct ktimer_node *node)
{
    //spin_lock_global(&heap_lock); 
    if (heap_size == heap_capacity) {
        if (!heap_capacity)
            heap_capacity = 3000;
        else
            heap_capacity <<= 1;
        heap_array = krealloc(heap_array, heap_capacity * sizeof(struct ktimer_node *));
    }

    heap_size++;

    int idx = heap_size - 1;
    heap_array[idx] = node;

    while (idx && heap_array[get_parent(idx)]->expiration_time > heap_array[idx]->expiration_time) {
        swap(idx, get_parent(idx));
        idx = get_parent(idx);
    }
    //spin_unlock_global(&heap_lock);
}

// ONLY call inside timer handler
static struct ktimer_node *timer_peek(void)
{
    spin_lock_global(&heap_lock); 

    if (!heap_size) {
        spin_unlock_global(&heap_lock); 
        return NULL;
    }

    struct ktimer_node *ret = heap_array[0];

    spin_unlock_global(&heap_lock);
    return ret;
}

// ONLY call inside timer handler
static struct ktimer_node *timer_pop(void)
{
    spin_lock_global(&heap_lock);

    if (!heap_size) {
        spin_unlock_global(&heap_lock);
        kpanic(0, NULL, "We should've peeked before popping anyways\n");
        return NULL;
    }

    if (heap_size == 1) {
        heap_size--;
        spin_unlock_global(&heap_lock);
        return heap_array[0];
    }

    struct ktimer_node *root = heap_array[0];
    heap_array[0] = heap_array[heap_size - 1];

    heap_size--;

    if (heap_size > MIN_TIMER_HEAP_SIZE && heap_capacity > (heap_size << 2)) {
        heap_capacity >>= 1;
        heap_array = krealloc(heap_array, heap_capacity * sizeof(struct ktimer_node *));
    }

    min_heapify(0);
    spin_unlock_global(&heap_lock);

    return root;
}

// this would be fun to do tickless
static void system_timer_handler(void)
{
    __atomic_add_fetch(&system_ticks, 1, __ATOMIC_SEQ_CST);
    if (system_ticks % 1000 == 0) {
        unix_time++;
    }

    // look if there are any timers ready to dispatch
    for (;;) {
        struct ktimer_node *timer = timer_peek();
        if (!timer || timer->expiration_time > system_ticks)
            break;

        timer = timer_pop();
        kevent_launch(&timer->event);
    }
}

// pass an (unitialized!) timer and an expiration timespan.
// kevent_t structure inside gets reset, and the calling thread
// can call kevents_poll() on tmr->event after returning.
// the thread will be woken up when the timer expired / the 
// timers handler has launched the corresponding event.
void register_system_timer(struct ktimer_node *tmr, size_t ms)
{
    // briefly disable interrupts since the timer lock ONLY inside insert()
    // can be taken inside the timer interrupt handler
    int_status_t s = preempt_fetch_disable();

    tmr->expiration_time = system_ticks + ms;
    tmr->event.lock.lock = 0;
    tmr->event.subscribers = NULL;
    tmr->event.unprocessed = 0;

    timer_insert(tmr);

    preempt_restore(s);
}


static void rtc_handler(cpu_ctx_t *regs)
{
    (void)regs;
    rtc_rd_int_type();

    lapic_send_eoi_signal();

    system_timer_handler();
}

// [TODO] test which timers are available and assign them here, hpet missing
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

    kprintf("%s initialized time\n", ansi_okay_string);
}