#pragma once

#include <stdint.h>
#include <stddef.h>

// handler[n] contains the, who guessed it, interrupt handler for vector n.
extern uintptr_t handlers[256];

typedef struct __attribute__((packed)) {
    // stack growns downwards hence flipped around
    uint64_t cr4;
    uint64_t cr3;
    uint64_t cr2;
    uint64_t cr0;
    uint64_t eflags;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;

    uint64_t vector;
    uint64_t error_code;
} INT_REG_INFO;

void exc_panic(INT_REG_INFO *regs);
void default_exception_handler(INT_REG_INFO *regs);

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} idt_descriptor;

void interrupts_register_vector(size_t vector, uintptr_t handler);
void interrupts_erase_vector(size_t vector);
void idt_set_descriptor(uint8_t vector, uintptr_t isr, uint8_t flags);
void init_idt(void);
void load_idt(void);
void kpanic(INT_REG_INFO *regs, uint8_t quiet, const char *format, ...);