#include "vector.h"

#include "liballoc.h"
#include "memory.h"

// sets initial values and reserves a first sizeof(size_t) bytes big array
void vector_init(vector *vec)
{
    vec->data = (uint8_t *)kmalloc(sizeof(size_t));
    vec->_size_allocated_bytes = sizeof(size_t);
    vec->_size_used_bytes = 0;
}

// push back
void vector_append(vector *vec, void *value, size_t bytes)
{
    if (vec->_size_allocated_bytes < vec->_size_used_bytes + bytes) {
        vec->_size_allocated_bytes *= 2;
        vec->data = krealloc((void *)vec->data, vec->_size_allocated_bytes);
    }
    memcpy((void *)(vec->data +vec->_size_used_bytes), value, bytes);
    vec->_size_used_bytes += bytes;
}

// reinit
void vector_reset(vector *vec)
{
    kfree((void *)vec->data);
    vector_init(vec);
}

// free
void vector_cleanup(vector *vec)
{
    kfree((void *)vec->data);
    vec->data = NULL;
    vec->_size_allocated_bytes = 0;
    vec->_size_used_bytes = 0;
}

// fill repeatedly with *value and remaining space with zeroes
void vector_fill_rep(vector *vec, void *value, size_t bytes)
{
    size_t i = 0;
    for (; i < vec->_size_allocated_bytes; i += bytes) {
        memcpy((void *)(vec->data + i), value, bytes);
    }
    memset((void *)vec->data, 0, vec->_size_allocated_bytes - i);
    vec->_size_used_bytes = vec->_size_allocated_bytes;
}

// set every index to value
inline void vector_fill(vector *vec, uint8_t value)
{
    memset((void *)vec->data, value, vec->_size_allocated_bytes);
    vec->_size_used_bytes = vec->_size_allocated_bytes;
}

// resizing downwards is a bad idea
inline void vector_resize(vector *vec, size_t bytes)
{
    krealloc((void*)vec->data, bytes);
    vec->_size_allocated_bytes = bytes;
}

// first byte index as (int) of *value for $bytes bytes, else -1
int vector_find_rep(vector *vec, void *value, size_t bytes)
{
    for (size_t i = 0; i < vec->_size_allocated_bytes; i += bytes) {
        if (!memcmp((void *)(vec->data + i), value, bytes)) {
            return i;
        }
    }
    return -1;
}

// first byte index as (int) of value == vec->data[i]
int vector_find(vector *vec, uint8_t value)
{
    for (size_t i = 0; i < vec->_size_allocated_bytes; i++) {
        if (vec->data[i] == value) {
            return i;
        }
    }
    return -1;
}