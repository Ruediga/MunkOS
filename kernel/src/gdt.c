#include <stdint.h>

#include "gdt.h"
#include "cpu.h"

struct __attribute__((packed)) {
    segment_descriptor gdts[5];
    system_segment_descriptor tss;
} gdt;

struct __attribute__((packed)) {
    uint16_t size;
    uintptr_t gdt_ptr;
} gdtr;

void init_gdt(void)
{
    // null descriptor
    gdt.gdts[0] = (segment_descriptor){ 0 };

    // 64 bit kernel code segment
    gdt.gdts[1] = (segment_descriptor){
        0, 0, 0,
        // Present, Segment, Executable code, Read- and Writeable
        0b10011010,
        // flags Granularity and Long mode
        0b10100000, 0
    };
    // 64 bit kernel data segment
    gdt.gdts[2] = (segment_descriptor){
        0, 0, 0,
        // Present, Segment, Read- and Writeable
        0b10010010,
        // flags Granularity and Long mode
        0b10100000, 0
    };
    // 64 bit user code segment
    gdt.gdts[3] = (segment_descriptor){
        0, 0, 0,
        // Present, Segment, Read- and Writeable
        0b11111010,
        // flags Granularity and Long mode
        0b10100000, 0
    };
    // 64 bit user data segment
    gdt.gdts[4] = (segment_descriptor){
        0, 0, 0,
        // Present, Segment, Read- and Writeable
        0b11110010,
        // flags Granularity and Long mode
        0b10100000, 0
    };

    // tss
    gdt.tss.descriptor.limit_low = sizeof(tss);
    gdt.tss.descriptor.base_low = 0;
    gdt.tss.descriptor.base_mid = 0;
    // Present, Executable, Accessed
    gdt.tss.descriptor.access_byte = 0b10001001;
    // size-bit
    gdt.tss.descriptor.limit_high_and_flags = 0;
    gdt.tss.descriptor.base_high = 0;
    gdt.tss.base = 0;
    gdt.tss.reserved = 0;

    // gdtr size - 1
    gdtr.size = (uint16_t)(sizeof(gdt) - 1);
    gdtr.gdt_ptr = (uintptr_t)&gdt;

    rld_gdt();
}

void rld_tss(struct task_state_segment *tss_address)
{
    static k_spinlock_t lock;

    acquire_lock(&lock);

    gdt.tss.base = (uint32_t)((uintptr_t)tss_address >> 32);
    gdt.tss.descriptor.limit_low = sizeof(tss);
    gdt.tss.descriptor.base_low = (uint16_t)((uintptr_t)tss_address);
    gdt.tss.descriptor.base_mid = (uint8_t)((uintptr_t)tss_address >> 16);
    // Present, Executable, Accessed
    gdt.tss.descriptor.access_byte = 0b10001001;
    // size-bit
    gdt.tss.descriptor.limit_high_and_flags = 0;
    gdt.tss.descriptor.base_high = (uint8_t)((uintptr_t)tss_address >> 24);
    gdt.tss.base = 0;
    gdt.tss.reserved = 0;

    __asm__ volatile (
        // load tss entry index: 5 * 8 byte
        "ltr %0"
        : : "rm" ((uint16_t)40) : "memory"
    );

    release_lock(&lock);
}

void rld_gdt()
{
    __asm__ volatile (
        "mov %0, %%rdi\n"
        "lgdt (%%rdi)\n"
        "push $0x8\n" // 8: offset kernel code 64 bit
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n" // 16: offset kernel data 64 bit
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