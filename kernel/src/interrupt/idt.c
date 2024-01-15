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
    idt_descriptor *descriptor = &idt[vector];

    descriptor->offset_low = isr & 0xFFFF;
    descriptor->selector = 0x8; // 8: kernel code selector offset (gdt)
    descriptor->ist = 0;
    descriptor->type_attributes = flags;
    descriptor->offset_mid = (isr >> 16) & 0xFFFF;
    descriptor->offset_high = (isr >> 32) & 0xFFFFFFFF;
    descriptor->reserved = 0;
}

void init_idt(void)
{
    // fill idt
    for (size_t vector = 0; vector <= 255; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
    }

    load_idt();
}

inline void load_idt(void)
{
    idtr.offset = (uintptr_t)idt;
    // max descriptors - 1
    idtr.size = (uint16_t)(sizeof(idt) - 1);

    __asm__ volatile (
        "lidt %0"
        : : "m"(idtr) : "memory"
    );
}