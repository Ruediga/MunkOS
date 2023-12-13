#include "interrupt/exception.h"

#include "std/kprintf.h"
void default_exception_handler(INT_REG_INFO *regs)
{
    printf("\nInterrupt 0x%lX called", regs->vector);
    printf("\nWith error code: %li", regs->error_code);

    printf("\nrdi: 0x%016lX", regs->rdi);
    printf("\nrsi: 0x%016lX", regs->rsi);
    printf("\nrbp: 0x%016lX", regs->rbp);
    printf("\nrsp: 0x%016lX", regs->rsp);
    printf("\nrbx: 0x%016lX", regs->rbx);
    printf("\nrdx: 0x%016lX", regs->rdx);
    printf("\nrcx: 0x%016lX", regs->rcx);
    printf("\nrax: 0x%016lX", regs->rax);
    printf("\nr8:  0x%016lX", regs->r8);
    printf("\nr9:  0x%016lX", regs->r9);
    printf("\nr10: 0x%016lX", regs->r10);
    printf("\nr11: 0x%016lX", regs->r11);
    printf("\nr12: 0x%016lX", regs->r12);
    printf("\nr13: 0x%016lX", regs->r13);
    printf("\nr14: 0x%016lX", regs->r14);
    printf("\nr15: 0x%016lX", regs->r15);

    printf("\nEFLAGS: 0x%016lX", regs->eflags);

    if (regs->vector != 0x69)
        asm volatile ("cli\n hlt");
}