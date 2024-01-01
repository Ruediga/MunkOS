#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "std/memory.h"
#include "std/kprintf.h"

// flanterm terminal
#include "flanterm/flanterm.h"
#include "flanterm/backends/fb.h"

#include "gdt.h"
#include "interrupt/idt.h"
#include "mm/pmm.h"
#include "mm/vmm.h"

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

LIMINE_BASE_REVISION(1)

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, in C, they should
// NOT be made "static".

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

// Halt and catch fire function.
void hcf(void)
{
    asm("cli");
    for (;;)
    {
        asm("hlt");
    }
}

// global flanterm context
struct flanterm_context *ft_ctx;

// linker puts the kernels highest address in here? [TODO]
extern uint64_t endkernel;

void kernel_entry(void)
{
    // set up new stack
    // [TODO]

    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    asm volatile("cli");

    // init flanterm (https://github.com/mintsuki/flanterm)
    ft_ctx = flanterm_fb_simple_init(
        framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch);

    printf("Limine framebuffer width: %lu, heigth: %lu\n", framebuffer->width, framebuffer->height);

    // load a GDT
    initGDT();
    printf("GDT set up\n");
   
    // initialize IDT
    initIDT();
    printf("Interrupts initialized\n");

    // pmm
    initPMM();
    printf("PMM functions prepared\n");

    // vmm
    initVMM();
    printf("VMM and kernel pagemap initialized\n");

    extern struct limine_hhdm_response *hhdm;
    uint64_t *arr = (uint64_t *)((uint64_t)pmmClaimContiguousPages(1) + hhdm->offset);
    memset(arr, 0xFF, PAGE_SIZE);

    asm volatile (
        "movq $50, %rdx\n"
        "xor %rax, %rax\n"
        "div %rdx\n"
    );
    // loop
    asm volatile("loop: jmp loop\n");

    // halt
    hcf();
}