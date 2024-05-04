.section .text

# setjmp-like task switch

# prototype: uint64_t save_task_context(struct jmpbuf *context);
.global save_task_context
.type save_task_context, @function
save_task_context:
    movq (%rsp), %rax
    movq %rax, 0x0(%rdi)

    # address of stack above the return address
    leaq 8(%rsp), %rax
    movq %rax, 0x8(%rdi)
    movq %rbp, 0x10(%rdi)

    movq %rbx, 0x18(%rdi)
    movq %r12, 0x20(%rdi)
    movq %r13, 0x28(%rdi)
    movq %r14, 0x30(%rdi)
    movq %r15, 0x38(%rdi)

    xor %rax, %rax

    retq


# prototype: void load_task_context(struct jmpbuf *context);
.global load_task_context
.type load_task_context, @function
load_task_context:
    # load callee saved registers
    movq 0x8(%rdi), %rsp
    movq 0x10(%rdi), %rbp

    movq 0x18(%rdi), %rbx
    movq 0x20(%rdi), %r12
    movq 0x28(%rdi), %r13
    movq 0x30(%rdi), %r14
    movq 0x38(%rdi), %r15

    mov $1, %rax

    # jump to rsp
    jmpq *0x0(%rdi)


# set up thread entry (offset 0x0) and thread data pointer (offset 0x8)
# for each kernel thread. this function then spins the threads up.
# why? because rdi is caller saved.
# prototype: void kernel_thread_spinup(void);
.global kernel_thread_spinup
.type kernel_thread_spinup, @function
kernel_thread_spinup:
    popq %rdi
    popq %rsi
    jmpq *%rsi