#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "scheduler.h"
#include "process.h"

// This is my attempt to make async easier, and allow for efficient thread synchronization.

#define KEVENT_POLL_INVALID (-1)

struct kevent_t;

typedef struct kevent_subscriber_t {
    // track all subscribed thread for an event type
    struct kevent_subscriber_t *next;
    struct kevent_subscriber_t *prev;

    struct kevent_t *event;

    struct task *subscribed_thread;
} kevent_subscriber_t;

typedef struct kevent_t {
    // events can be launched multiple times (for example from a keyboard driver)
    // without being polled.
    size_t unprocessed;

    // whenever an event is being polled, we track all threads waiting for it's completion
    kevent_subscriber_t *subscribers;

    // prevent concurrent accesses to this events subscriber list
    k_spinlock_t lock;
} kevent_t;

// wait for any of the events in the queue to launch, return the launched events index.
// caller is responsible for removing any unused events from the queue.
// return KEVENT_POLL_INVALID upon failure.
int kevents_poll(kevent_t **events, int event_count);
// inc unprocessed counter for event
void kevent_launch(kevent_t *event);