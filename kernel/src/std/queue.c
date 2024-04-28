#include "queue.h"
#include "kheap.h"

void queue_enqueue(queue_t *queue, void *value)
{
    if (!queue->root) {
        queue->root = (queue_node_t *)kmalloc(sizeof(queue_node_t));
        queue->root->next = NULL;
        queue->root->value = value;
        queue->head = queue->root;
        queue->_size++;
        return;
    }

    queue->head->next = (queue_node_t *)kmalloc(sizeof(queue_node_t));
    queue->head = queue->head->next;
    queue->head->next = NULL;
    queue->head->value = value;
    queue->_size++;
}

// return: NULL on empty list, value of fifo element
void *queue_dequeue(queue_t *queue)
{
    if (!queue->root) {
        return NULL;
    }

    void *out = queue->root->value;
    if (!queue->root->next) {
        kfree(queue->root);
        queue->root = NULL;
        queue->head = NULL;
        queue->_size = 0;
        return out;
    } else {
        queue_node_t *prev = queue->root->next;
        kfree(queue->root);
        queue->root = prev;
        queue->_size--;
    }
    return out;
}