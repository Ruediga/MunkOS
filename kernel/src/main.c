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

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

LIMINE_BASE_REVISION(1)

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, in C, they should
// NOT be made "static".

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

// Halt and catch fire function.
static void hcf(void) {
    asm ("cli");
    for (;;) {
        asm ("hlt");
    }
}

// global flanterm context
struct flanterm_context *ft_ctx;

// The following will be our kernel's entry point.
// If renaming _start() to something else, make sure to change the
// linker script accordingly.
void _start(void) {
    // set up new stack
    // [TODO]

    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // ==================
    // here MunkOS starts

    // init flanterm (source: https://github.com/mintsuki/flanterm)
    ft_ctx = flanterm_fb_simple_init(
        framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch
    );

    // load a GDT
    initGDT();
    printf("GDT set up\n\r");

    // read and print memory map
    if (memmap_request.response == NULL
     || memmap_request.response->entry_count <= 1) {
        hcf();
    }
    struct limine_memmap_entry *memmap = memmap_request.response->entries[0];

    uint64_t total_memory = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        printf("Entry %-2lu: Base = 0x%016lx, Length = 0x%016lx, Type = %lu\n\r", i, memmap[i].base, memmap[i].length, memmap[i].type);
        if (memmap[i].type == 0)
            total_memory += memmap[i].length;
    }
    printf("Done printing memmap, total memory: 0x%016lx\n\r", total_memory);

    // initialize IDT
    initIDT();

    // done
    hcf();
}
