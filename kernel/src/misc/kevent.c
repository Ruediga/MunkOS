#include "kevent.h"
#include "compiler.h"

#define KEVENT_SUBSCRIBER_POOL_REFILL_AMOUNT (128)

// keep a small pool of kevent_subscriber_t's to avoid calling kmalloc too often
static kevent_subscriber_t *subscriber_pool;

static inline kevent_subscriber_t *_subscriber_pool_alloc(void)
{
    if (!subscriber_pool) {
        // empty: fill up again
        for (int i = 0; i < KEVENT_SUBSCRIBER_POOL_REFILL_AMOUNT; i++) {
            kevent_subscriber_t *old = subscriber_pool;
            // It'd be a better idea to use slab caches here since this is
            // a ton of overhead since this allocations only happen a few times
            // during system initalization i dont care enough
            subscriber_pool = kmalloc(sizeof(kevent_subscriber_t));
            subscriber_pool->next = old;
        }
    }

    kevent_subscriber_t *ret = subscriber_pool;
    subscriber_pool = subscriber_pool->next;
    return ret;
}

static inline void _subscriber_pool_free(kevent_subscriber_t *s)
{
    kevent_subscriber_t *old = subscriber_pool;
    subscriber_pool = s;
    subscriber_pool->next = old;
}

static inline void _subscriber_list_link(kevent_t *ev, struct task *t) {
    kevent_subscriber_t *new = _subscriber_pool_alloc();
    new->event = ev;
    new->subscribed_thread = t;

    new->prev = NULL;
    new->next = ev->subscribers;
    if (ev->subscribers)
        (ev->subscribers)->prev = new;
    ev->subscribers = new;
}

// unlink entry from dll
static inline void _subscriber_list_unlink(kevent_t *ev, struct task *t) {
    kevent_subscriber_t *curr = ev->subscribers;
    while (curr) {
        if (curr->subscribed_thread != t) {
            curr = curr->next;
            continue;
        }

        goto found;
    }

    kpanic(0, NULL, "struct task *t wasn't subscribed to kevent_t *ev\n");
    unreachable();

found:
    if (!ev->subscribers) kpanic(0, NULL, "head == 0");

    if (ev->subscribers == curr)
        ev->subscribers = curr->next;

    if (curr->prev)
        curr->prev->next = curr->next;
    if (curr->next)
        curr->next->prev = curr->prev;

    _subscriber_pool_free(curr);
}

static inline int _find_unprocessed(kevent_t **events, int event_count)
{
    // try to find any unprocessed event
    for (int i = 0; i < event_count; i++) {
        kevent_t *ev = events[i];

        spin_lock(&ev->lock);
        if (ev->unprocessed) {
            ev->unprocessed--;
            spin_unlock(&ev->lock);
            return i;
        }
        spin_unlock(&ev->lock);
    }

    return KEVENT_POLL_INVALID;
}

// link a kevent_subscriber_t to each event for this thread
static void kevent_subscribe(kevent_t **events, int event_count, struct task *this)
{
    for (int i = 0; i < event_count; i++) {
        kevent_t *ev = events[i];

        spin_lock(&ev->lock);
        _subscriber_list_link(ev, this);
        spin_unlock(&ev->lock);
    }
}

// unlink a kevent_subscriber_t from each even for this thread
static void kevent_unsubscribe(kevent_t **events, int event_count, struct task *this)
{
    for (int i = 0; i < event_count; i++) {
        kevent_t *ev = events[i];

        spin_lock(&ev->lock);
        _subscriber_list_unlink(ev, this);
        spin_unlock(&ev->lock);
    }
}

// wait for any of the events in the queue to launch, return its index.
// caller is responsible for removing any unused events from the queue (e.g. a file io
// event that gets launched only once, unlike a keyboard char device that will never
// stop launching events)
int kevents_poll(kevent_t **events, int event_count)
{
    int_status_t intstate = ints_fetch_disable();

    int out = KEVENT_POLL_INVALID;

    struct task *this_thread = scheduler_curr_task();

    // try to find any unprocessed event
    out = _find_unprocessed(events, event_count);

    // we found an unprocessed event
    if (out != KEVENT_POLL_INVALID) {
        ints_status_restore(intstate);
        return out;
    }

    // wait for event to launch, sleep in the meantime
    kevent_subscribe(events, event_count, this_thread);
    scheduler_put_task2sleep(this_thread);
    scheduler_yield();

    // we got woken up again
    // maybe store this in the task context
    out = _find_unprocessed(events, event_count);
    // do this so events can get removed between consecutive calls to kevents_poll()
    kevent_unsubscribe(events, event_count, this_thread);

    ints_status_restore(intstate);
    return out;
}

// inc unprocessed counter for event
void kevent_launch(kevent_t *event)
{
    if (!event->subscribers)
        kpanic(0, NULL, "Should we be able to launch events with no subscribers?\n");

    int_status_t istate = ints_fetch_disable();
    spin_lock(&event->lock);

    event->unprocessed++;

    // attempt to wake all sleeping threads waiting for this event
    kevent_subscriber_t *curr = event->subscribers;
    while (curr) {
        // maybe store this in the tctx

        scheduler_wake(curr->subscribed_thread);
        curr = curr->next;
    }

    spin_unlock(&event->lock);
    ints_status_restore(istate);
}