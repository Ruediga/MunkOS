#include <stdint.h>

#include "gdt.h"

typedef struct {

} __attribute__((packed)) tss_entry;

struct {
    uint16_t size;
    uint64_t gdt_ptr;
} __attribute__((packed)) gdtr;

gdt_entry gdt[5];

void initGDT(void)
{
    // clear interrupts
    asm ("cli");

    // null descriptor
    gdt[0].limit_low = 0;
    gdt[0].base_low = 0;
    gdt[0].base_mid = 0;
    gdt[0].access_byte = 0;
    gdt[0].limit_high_and_flags = 0;
    gdt[0].base_high = 0;

    // kernel code segment
    gdt[1].limit_low = 0xFFFF;
    gdt[1].base_low = 0;
    gdt[1].base_mid = 0;
    // Present, Segment, Executable code, Read- and Writeable
    gdt[1].access_byte = 0b10011010;
    // flags Granularity and Long mode, limit 0xF
    gdt[1].limit_high_and_flags = 0b10101111;
    gdt[1].base_high = 0;

    // kernel data segment
    gdt[2].limit_low = 0xFFFF;
    gdt[2].base_low = 0;
    gdt[2].base_mid = 0;
    // Present, Segment, Read- and Writeable
    gdt[2].access_byte = 0b10010010;
    // flags Granularity (mode irrelevant), limit 0xF
    gdt[2].limit_high_and_flags = 0b10101111;
    gdt[2].base_high = 0;

    // user code segment
    gdt[3].limit_low = 0xFFFF;
    gdt[3].base_low = 0;
    gdt[3].base_mid = 0;
    // Pesent, DPL ring 3, Segment, Executable code, Read- and Writeable
    gdt[3].access_byte = 0b11111010;
    // flags Granularity and Long mode, limit 0xF
    gdt[3].limit_high_and_flags = 0b10101111;
    gdt[3].base_high = 0;

    // kernel data segment
    gdt[4].limit_low = 0xFFFF;
    gdt[4].base_low = 0;
    gdt[4].base_mid = 0;
    // Present, DPL ring 3, Segment, Read- and Writeable
    gdt[4].access_byte = 0b11110010;
    // flags Granularity (mode irrelevant), limit 0xF
    gdt[4].limit_high_and_flags = 0b10101111;
    gdt[4].base_high = 0;

    // tss?
    // [TODO]

    // gdtr
    gdtr.size = sizeof(gdt);
    gdtr.gdt_ptr = (uint64_t)gdt;

    loadGDT();
}

void loadGDT(void)
{
    asm volatile (
        "mov %0, %%rdi\n"
        "lgdt (%%rdi)\n"
        "push $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "retq"
        :
        : "r" (&gdtr)
        : "memory"
    );
}