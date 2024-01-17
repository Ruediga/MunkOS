#include "memory.h"
#include "interrupt.h"
#include "kprintf.h"
#include "cpu.h"

uintptr_t handlers[256] = {0};

static k_spinlock int_register_vec_lock;
static k_spinlock int_erase_vec_lock;

void default_exception_handler(INT_REG_INFO *regs)
{
    // intel sdm vol 3, 6VMM_HIGHER_HALF.1 table 6-1
    switch (regs->vector)
    {
    case 0:
        exc_panic(regs, "Division by Zero", 0);
        break;
    case 1:
        exc_panic(regs, "Debug", 0);
        break;
    case 2:
        exc_panic(regs, "Non-Maskable-Interrupt", 0);
        break;
    case 3:
        exc_panic(regs, "Breakpoint", 0);
        break;
    case 4:
        exc_panic(regs, "Overflow", 0);
        break;
    case 5:
        exc_panic(regs, "Bound Range Exceeded", 0);
        break;
    case 6:
        exc_panic(regs, "Invalid opcode", 0);
        break;
    case 7:
        exc_panic(regs, "Device (FPU) not available", 0);
        break;
    case 8:
        exc_panic(regs, "Double Fault", 0);
        break;
    case 10:
        exc_panic(regs, "Invalid TSS, ", 1);
        break;
    case 11:
        exc_panic(regs, "Segment not present, ", 1);
        break;
    case 12:
        exc_panic(regs, "Stack Segment Fault, ", 1);
        break;
    case 13:
        exc_panic(regs, "General Protection Fault, ", 1);
        break;
    case 14:
        exc_panic(regs, "Page Fault, ", 1);
        break;
    case 16:
        exc_panic(regs, "x87 FP Exception", 0);
        break;
    case 17:
        exc_panic(regs, "Alignment Check, ", 1);
        break;
    case 18:
        exc_panic(regs, "Machine Check (Internal Error)", 0);
        break;
    case 19:
        exc_panic(regs, "SIMD FP Exception", 0);
        break;
    case 20:
        exc_panic(regs, "Virtualization Exception", 0);
        break;
    case 21:
        exc_panic(regs, "Control  Protection Exception, ", 1);
        break;
    case 28:
        exc_panic(regs, "Hypervisor Injection Exception", 0);
        break;
    case 29:
        exc_panic(regs, "VMM Communication Exception, ", 1);
        break;
    case 30:
        exc_panic(regs, "Security Exception, ", 1);
        break;
    default:
        exc_panic(regs, "Unhandled Interrupt occured", 0);
    }

    return;
}

// cpu exception panic
void exc_panic(INT_REG_INFO *regs, const char *msg, size_t print_error_code)
{
    // red bg
    kprintf("\033[41m");

    kprintf("\n[IV 0x%lX] -> ", regs->vector);
    kprintf(msg);
    print_error_code ? kprintf("[ec 0x%lX]:\n\r", regs->error_code) : kprintf(":\n\r");

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

    kprintf("EFLAGS: 0x%b\n\n", regs->eflags);

    for (;;) __asm__ volatile("cli\n hlt\n");
}

void empty_handler(INT_REG_INFO *regs)
{
    (void)regs;
    kprintf("Unhandled interrupt occured...\n");
    __asm__ ("hlt");
}

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
    for (size_t vector = 0; vector < 256; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
    }
    for (size_t vector = 0; vector < 32; vector++) {
        handlers[vector] = (uintptr_t)default_exception_handler;
    }
    for (size_t vector = 32; vector < 256; vector++) {
        handlers[vector] = (uintptr_t)empty_handler;
    }

    load_idt();
}

void interrupts_register_vector(size_t vector, uintptr_t handler)
{
    acquire_lock(&int_register_vec_lock);
    if (handlers[vector] != (uintptr_t)empty_handler || !handler) {
        // panic
        kprintf("Failed to register vector %lu at 0x%p\n", vector, handler);
        __asm__ ("hlt");
    }
    handlers[vector] = handler;
    release_lock(&int_register_vec_lock);
}

void interrupts_erase_vector(size_t vector)
{
    acquire_lock(&int_erase_vec_lock);
    if (handlers[vector] == (uintptr_t)empty_handler || vector < 32) {
        // panic
        kprintf("Failed to erase vector %lu\n", vector);
        __asm__ ("hlt");
    }
    handlers[vector] = (uintptr_t)empty_handler;
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