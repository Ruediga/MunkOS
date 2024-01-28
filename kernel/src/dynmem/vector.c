#include "vector.h"

#include "liballoc.h"
#include "memory.h"

/*
 * vector_t structure to work with same sized blocks of data,
 * be careful not to append data of the wrong length
 * Don't modify _size, _capacity or _element_size
*/

// call this only on vectors with no data
void vector_init(vector_t *vec, size_t elem_size)
{
    vec->data = NULL;
    vec->_size = 0;
    vec->_capacity = 0;
    vec->_element_size = elem_size;
}

// returns index
size_t vector_append(vector_t *vec, void *value)
{
    if (vec->_capacity == vec->_size) {
        vec->_capacity = (!vec->_capacity ? 1 : vec->_capacity * 2);
        vec->data = krealloc(vec->data, vec->_capacity * vec->_element_size);
    }

    memcpy((uint8_t *)vec->data + vec->_size * vec->_element_size, value, vec->_element_size);
    vec->_size++;
    return vec->_size - 1;
}

// return 1 on success, 0 on fail
size_t vector_remove(vector_t *vec, size_t idx)
{
    if (idx >= vec->_size)
        return 0;

    // move elems after idx one backward
    memmove((uint8_t *)vec->data + idx * vec->_element_size,
            (uint8_t *)vec->data + (idx + 1) * vec->_element_size,
            (vec->_size - idx - 1) * vec->_element_size);

    vec->_size--;

    if (vec->_size < vec->_capacity / 2) {
        vec->_capacity = vec->_capacity / 2;
        vec->data = krealloc(vec->data, vec->_capacity * vec->_element_size);
    }

    return 1;
}

// fills all allocated space repeatedly with *value
void vector_fill(vector_t *vec, void *value)
{
    memcpy(vec->data, value, vec->_capacity * vec->_element_size);
}

// reserve space for *length elements
void vector_resize(vector_t *vec, size_t length)
{
    // if made smaller
    if (vec->_capacity > length)
        vec->_size = length;

    vec->_capacity = length;
    vec->data = krealloc(vec->data, vec->_capacity * vec->_element_size);
}

void vector_reset(vector_t *vec)
{
    kfree(vec->data);
    vector_init(vec, vec->_element_size);
}

size_t vector_find(vector_t *vec, void *value)
{
    for (size_t i = 0; i < vec->_size; i++) {
        if (!memcmp((uint8_t *)vec->data + i * vec->_element_size, value, vec->_element_size))
            return i;
    }
    return (size_t)-1;
}