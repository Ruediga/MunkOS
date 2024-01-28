#include "memory.h"
#include "interrupt.h"
#include "kprintf.h"
#include "cpu.h"
#include "apic.h"
#include "scheduler.h"

#include <stdarg.h>

uintptr_t handlers[256] = {0};

static k_spinlock_t int_register_vec_lock;
static k_spinlock_t int_erase_vec_lock;

struct __attribute__((packed)) {
    uint16_t size;
    uint64_t offset;
} idtr;

__attribute__((aligned(16))) idt_descriptor idt[256];

extern uint64_t isr_stub_table[];

static const char * cpu_exception_strings[32] = {
    "Division by Zero",
    "Debug",
    "Non-Maskable-Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid opcode",
    "Device (FPU) not available",
    "Double Fault",
    "RESERVED VECTOR",
    "Invalid TSS, ",
    "Segment not present, ",
    "Stack Segment Fault, ",
    "General Protection Fault, ",
    "Page Fault, ",
    "RESERVED VECTOR",
    "x87 FP Exception",
    "Alignment Check, ",
    "Machine Check (Internal Error)",
    "SIMD FP Exception",
    "Virtualization Exception",
    "Control  Protection Exception, ",
    "RESERVED VECTOR",
    "RESERVED VECTOR",
    "RESERVED VECTOR",
    "RESERVED VECTOR",
    "RESERVED VECTOR",
    "RESERVED VECTOR",
    "Hypervisor Injection Exception",
    "VMM Communication Exception, ",
    "Security Exception, ",
    "RESERVED VECTOR"
};

void default_interrupt_handler(cpu_ctx_t *regs)
{
    kpanic(regs, "An unhandled interrupt occured\n");
}

void cpu_exception_handler(cpu_ctx_t *regs)
{
    kpanic(regs, "");
}

void print_register_context(cpu_ctx_t *regs)
{
    kprintf("\n[IV %lu] -> %s", regs->vector, regs->vector < 32 ? 
        cpu_exception_strings[regs->vector] : "?\n");

    if (regs->vector == 8 || regs->vector == 10 || regs->vector == 11 || regs->vector == 12
        || regs->vector == 13 || regs->vector == 14 || regs->vector == 17 || regs->vector == 30)
        kprintf("[ec 0x%lX]:\n\r", regs->error_code);

    kprintf("rdi: 0x%p   rsi: 0x%p   rbp: 0x%p   rsp: 0x%p\
\nrbx: 0x%p   rdx: 0x%p   rcx: 0x%p   rax: 0x%p\n",
        regs->rdi, regs->rsi, regs->rbp, regs->rsp,
        regs->rbx, regs->rdx, regs->rcx, regs->rax);

    kprintf("r8:  0x%p   r9:  0x%p   r10: 0x%p   r11: 0x%p\
\nr12: 0x%p   r13: 0x%p   r14: 0x%p   r15: 0x%p\n",
        regs->r8, regs->r9, regs->r10, regs->r11,
        regs->r12, regs->r13, regs->r14, regs->r15);

    kprintf("cr0: 0x%p   cr2: 0x%p   cr3: 0x%p   cr4: 0x%p\n",
        regs->cr0, regs->cr2, regs->cr3, regs->cr4);

    kprintf("ss: 0x%p   cs: 0x%p   ds: 0x%p   es: 0x%p\n",
        regs->ss, regs->cs, regs->ds, regs->es);

    kprintf("EFLAGS: 0x%b\n\n", regs->rflags);
}

// kernel panic
void __attribute__((noreturn)) kpanic(cpu_ctx_t *regs, const char *format, ...)
{
    __asm__ ("cli");    // if manually called interrupts may be on

    //release_lock(&kprintf_lock); // kprintf may be locked ?

    kprintf("\n\n\033[41m<-- KERNEL PANIC -->\n\r");

    va_list args;
    va_start(args, format);
    kvprintf(format, args);
    va_end(args);

    if (regs) print_register_context(regs);

    // halt other cores
    cpu_local_t *this_cpu = get_this_cpu();
    kprintf("Halting cores: %lu", this_cpu->id);
    lapic_send_ipi(0, INT_VEC_LAPIC_IPI, ICR_DEST_OTHERS);

    __asm__ ("hlt");

    __builtin_unreachable();
}

void idt_set_descriptor(uint8_t vector, uintptr_t isr, uint8_t flags)
{
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
    for (size_t vector = 0; vector < 32; vector++) {
        // Present [7], kernel only [6:5], 0 [4], gate type [3:0]
        idt_set_descriptor(vector, isr_stub_table[vector], 0b10001111);
        handlers[vector] = (uintptr_t)cpu_exception_handler;
    }
    for (size_t vector = 32; vector < 256; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0b10001110);
        handlers[vector] = (uintptr_t)default_interrupt_handler;
    }

    load_idt();
}

void interrupts_register_vector(size_t vector, uintptr_t handler)
{
    acquire_lock(&int_register_vec_lock);
    if (handlers[vector] != (uintptr_t)default_interrupt_handler || !handler) {
        // panic
        kprintf("Failed to register vector %lu at 0x%p\n", vector, handler);
    }
    handlers[vector] = handler;
    release_lock(&int_register_vec_lock);
}

void interrupts_erase_vector(size_t vector)
{
    acquire_lock(&int_erase_vec_lock);
    if (handlers[vector] == (uintptr_t)default_interrupt_handler || vector < 32) {
        // panic
        kprintf("Failed to erase vector %lu\n", vector);
        __asm__ ("hlt");
    }
    handlers[vector] = (uintptr_t)default_interrupt_handler;
    release_lock(&int_erase_vec_lock);
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