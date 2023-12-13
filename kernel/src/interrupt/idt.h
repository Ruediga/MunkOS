#pragma once

void idt_set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags);
void initIDT(void);