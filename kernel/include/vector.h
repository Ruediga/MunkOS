#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct
{
    void *data;
    size_t _size;
    size_t _capacity;
    size_t _element_size;
} vector_t;

static inline size_t vector_size(vector_t *vec) {
    return vec->_size;
}

static inline void *vector_at(vector_t *vec, size_t idx) {
    return ((uint8_t *)vec->data + idx * vec->_element_size);
}

// quick init method
#define VECTOR_INIT_FAST(size) { NULL, 0, 0, size }

void vector_init(vector_t *vec, size_t elem_size);
size_t vector_append(vector_t *vec, void *value);
void vector_fill(vector_t *vec, void *value);
void vector_resize(vector_t *vec, size_t length);
void vector_reset(vector_t *vec);
size_t vector_find(vector_t *vec, void *value);
bool vector_remove_idx(vector_t *vec, size_t idx);
bool vector_remove_val(vector_t *vec, void *val);