#include "interrupt/idt.h"
#include "interrupt/exception.h"
#include "std/kprintf.h"
#include "idt.h"

struct __attribute__((packed)) {
    uint16_t size;
    uint64_t offset;
} idtr;

__attribute__((aligned(16))) idt_descriptor idt[256];

extern uint64_t isr_stub_table[];

void idt_set_descriptor(uint8_t vector, uintptr_t isr, uint8_t flags) {
    idt[vector].offset_low = isr & 0xFFFF;
    idt[vector].selector = 0x8; // 8: kernel code selector offset (gdt)
    idt[vector].ist = 0;
    idt[vector].type_attributes = flags;
    idt[vector].offset_mid = (isr >> 16) & 0xFFFF;
    idt[vector].offset_high = (isr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void initIDT(void)
{
    // fill idt
    for (size_t vector = 0; vector < 256; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
    }
}

inline void loadIDT(void)
{
    idtr.offset = (uintptr_t)idt;
    // max descriptors - 1
    idtr.size = (uint16_t)(sizeof(idt) - 1);

    asm volatile (
        "lidt %0"
        : : "m"(idtr) : "memory"
    );
}