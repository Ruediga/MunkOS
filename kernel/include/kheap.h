#pragma once

#include <stddef.h>
#include <stdint.h>

extern uintptr_t kernel_heap_base_address;

void init_kernel_heap(size_t max_heap_size_pages);