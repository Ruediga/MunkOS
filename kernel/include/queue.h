#pragma once

#include <stddef.h>
#include <stdint.h>

// FIFO queue

typedef struct queue_node_t {
    void *value;
    struct queue_node_t *next;
} queue_node_t;

typedef struct {
    queue_node_t *root;
    queue_node_t *head;
    size_t _size;
} queue_t;

#define QUEUE_INIT_FAST() { NULL, NULL, 0 }

void queue_enqueue(queue_t *queue, void *value);
void *queue_dequeue(queue_t *queue);

static inline int queue_is_empty(queue_t *queue) {
    return queue->_size == 0;
}

static inline size_t queue_size(queue_t *queue) {
    return queue->_size;
}