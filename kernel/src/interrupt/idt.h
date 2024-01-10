#pragma once

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} idt_descriptor;

void idt_set_descriptor(uint8_t vector, uintptr_t isr, uint8_t flags);
void initIDT(void);
void loadIDT(void);