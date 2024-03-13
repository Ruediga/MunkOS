.globl handlers
.extern handlers

.macro _swapgs_if_user
    cmpq $24, 24(%rsp) # CS (kernel code 64 bit): does the interrupt come from usermode?
    je 1f
    swapgs
	#movl $0xC0000101, %ecx
	#rdmsr
	#testl %edx, %edx
	#je 1f
	#swapgs
	#xorl %ebx, %ebx
1:
.endm

isr_handler_stub:
    # SS and SP for stack switch obtained from TSS
    # SS and SP of interrupted procedure get pushed onto new stack
    # (no privilege lvl change doesn't push these two)
    # RFLAGS
    # CS
    # RIP
    # Error Code
    # (manually pushed vector)
    _swapgs_if_user
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %cr0, %rax
    pushq %rax
    movq %cr2, %rax
    pushq %rax
    movq %cr3, %rax
    pushq %rax
    movq %cr4, %rax
    pushq %rax

    movq %ds, %rax
    pushq %rax
    movq %es, %rax
    pushq %rax

    # void default_exception_handler(cpu_ctx_t *regs)
    movq %rsp, %rdi
    movq 168(%rsp), %rbx        # read n from the stack
    shlq $3, %rbx               # offset = n * 8
    leaq handlers(%rip), %rax   # load the pointers base address
    addq %rbx, %rax             # base + offset
    callq *(%rax)               # go to where rax value points and call this address

    # cr0 - cr4 and pushfq
    addq $48, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rbp
    popq %rsi
    popq %rdi
    # Clean up pushed error code and interrupt number from stack
    addq $16, %rsp

    _swapgs_if_user

    # Pop other flags and return to normal execution State
    iretq

# generates a handler to jump to the common ISR handler function,
# taking interrupts with error codes into consideration
.macro ISR index
.global _isr\index
.type _isr\index, @function
_isr\index:
.if 0x\index != 8 && 0x\index != 10 && 0x\index != 11 && 0x\index != 12 && 0x\index != 13 && 0x\index != 14 && 0x\index != 17 && 0x\index != 30
    pushq $0
.endif
    pushq $0x\index
    jmp isr_handler_stub
.endm

# retrieve the address of an ISR stump
.macro ISRADDR index
    .quad _isr\index
.endm


# generate all the stubs
.irpc i, 0123456789abcdef
.irpc j, 0123456789abcdef
    ISR \i\j
.endr
.endr

.section .data

# generate a uint64_t array of addresses pointing
# to each one of the stubs
.global isr_stub_table
.align 8
isr_stub_table:
.irpc i, 0123456789abcdef
.irpc j, 0123456789abcdef
    ISRADDR \i\j
.endr
.endr