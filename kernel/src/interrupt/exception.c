#include "interrupt/exception.h"

#include "std/kprintf.h"
void default_exception_handler(INT_REG_INFO *regs)
{
    printf("\nInterrupt 0x%lX called with error code: %lu\n",
        regs->vector, regs->error_code);

    printf("rdi: 0x%016lX   rsi: 0x%016lX   rbp: 0x%016lX   rsp: 0x%016lX\
        \nrbx: 0x%016lX   rdx: 0x%016lX   rcx: 0x%016lX   rax: 0x%016lX\n",
        regs->rdi, regs->rsi, regs->rbp, regs->rsp,
        regs->rbx, regs->rdx, regs->rcx, regs->rax);
    printf("r8:  0x%016lX   r9:  0x%016lX   r10: 0x%016lX   r11: 0x%016lX\
        \nr12: 0x%016lX   r13: 0x%016lX   r14: 0x%016lX   r15: 0x%016lX\n",
        regs->r8, regs->r9, regs->r10, regs->r11,
        regs->r12, regs->r13, regs->r14, regs->r15);

    printf("cr0: 0x%016lX   cr2: 0x%016lX   cr3: 0x%016lX   cr4: 0x%016lX\n",
        regs->cr0, regs->cr2, regs->cr3, regs->cr4);

    printf("EFLAGS: 0x%lX\n\n", regs->eflags);

    asm volatile("cli\n hlt\n");
}