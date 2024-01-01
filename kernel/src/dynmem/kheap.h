#pragma once

#include <stddef.h>
#include <stdint.h>

extern uint64_t kernel_heap_max_size;
extern uintptr_t kernel_heap_base_address;

void initializeKernelHeap(void);