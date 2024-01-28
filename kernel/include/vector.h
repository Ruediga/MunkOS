#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    void *data;
    size_t _size;
    size_t _capacity;
    size_t _element_size;
} vector_t;

// quick init method
#define VECTOR_INIT_FAST(size) { NULL, 0, 0, size }

void vector_init(vector_t *vec, size_t elem_size);
size_t vector_append(vector_t *vec, void *value);
void vector_fill(vector_t *vec, void *value);
void vector_resize(vector_t *vec, size_t length);
void vector_reset(vector_t *vec);
size_t vector_find(vector_t *vec, void *value);