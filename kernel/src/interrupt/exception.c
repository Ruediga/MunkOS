#include "interrupt/exception.h"
#include "std/memory.h"

// [TODO] remove
#include "driver/ps2_keyboard.h"
#include "limine.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "cpu/io.h"
#include "apic/lapic.h"

#include "flanterm/flanterm.h"
extern struct flanterm_context *ft_ctx;

#include "std/kprintf.h"
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
    case 0x99:
        uint8_t input_byte = ps2_read();
        kprintf("%lu ",  (uint64_t)input_byte);
        lapic_send_eoi_signal();
        break;
    default:
        exc_panic(regs, "Unhandled Exception thrown", 0);
    }

    return;
}

// cpu exception panic
void exc_panic(INT_REG_INFO *regs, const char *msg, size_t print_error_code)
{
    // red bg
    flanterm_write(ft_ctx, "\033[41m", 6);

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