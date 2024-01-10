#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint8_t *data;
    size_t _size_used_bytes;
    size_t _size_allocated_bytes;
} vector;

void vector_init(vector *vec);
void vector_append(vector *vec, void *value, size_t bytes);
void vector_reset(vector *vec);
void vector_cleanup(vector *vec);
void vector_fill_rep(vector *vec, void *value, size_t bytes);
void vector_fill(vector *vec, uint8_t value);
void vector_resize(vector *vec, size_t bytes);
int vector_find_rep(vector *vec, void *value, size_t bytes);
int vector_find(vector *vec, uint8_t value);