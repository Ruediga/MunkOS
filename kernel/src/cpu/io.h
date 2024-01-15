#pragma once

#include <stdint.h>

inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

inline uint8_t inb(uint16_t port)
{
    uint8_t out;
    __asm__ volatile("inb %1, %0" : "=a"(out) : "Nd"(port) : "memory");
    return out;
}

inline void io_wait(void)
{
    outb(0x80, 0);
}

inline uint64_t read_msr(uint32_t reg)
{
    uint32_t eax = 0, edx = 0;
   __asm__ volatile(
        "rdmsr"
        : "=a"(eax), "=d"(edx)
        : "c"(reg)
        :"memory"
    );
   return ((uint64_t)eax | (uint64_t)edx << 32);
}

inline void write_msr(uint32_t reg, uint64_t value)
{
    __asm__ volatile(
        "wrmsr"
        :
        : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)), "c"(reg)
        : "memory"
    );
}