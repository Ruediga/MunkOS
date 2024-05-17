#pragma once

#include <stdint.h>
#include <stddef.h>

#include "compiler.h"

// interrupt mappings. 0 - 31 cpu
#define INT_VEC_PIT 32
#define INT_VEC_RTC 33
#define INT_VEC_PS2 34

#define INT_VEC_SCHEDULER 100
#define INT_VEC_LAPIC_TIMER 101

#define INT_VEC_LAPIC_IPI 200

#define INT_VEC_SPURIOUS 254
#define INT_VEC_GENERAL_PURPOSE 253 // this may only be used for locked operations and has to be freed

#define KPANIC_FLAGS_QUIET (1 << 0)
#define KPANIC_FLAGS_THIS_CORE_ONLY (1 << 1)
#define KPANIC_FLAGS_DONT_TRACE_STACK (1 << 2)

extern uintptr_t handlers[256];

typedef struct comp_packed {
    // stack growns downwards hence flipped around
    uint64_t es;
    uint64_t ds;
    uint64_t cr4;
    uint64_t cr3;
    uint64_t cr2;
    uint64_t cr0;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t vector;

    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} cpu_ctx_t;

typedef struct comp_packed {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} idt_descriptor;

void default_interrupt_handler(cpu_ctx_t *regs);
void cpu_exception_handler(cpu_ctx_t *regs);
void print_register_context(cpu_ctx_t *regs);
void kpanic(uint8_t flags, cpu_ctx_t *regs, const char *format, ...);
void idt_set_descriptor(uint8_t vector, uintptr_t isr, uint8_t flags);
void init_idt(void);
void interrupts_register_vector(size_t vector, uintptr_t handler);
void interrupts_erase_vector(size_t vector);
void load_idt(void);