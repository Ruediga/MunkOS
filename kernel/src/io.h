#pragma once

#include <stdint.h>

inline void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

inline uint8_t inb(uint16_t port)
{
    uint8_t out;
    asm volatile("inb %1, %0" : "=a"(out) : "Nd"(port) : "memory");
    return out;
}

inline void io_wait(void)
{
    outb(0x80, 0);
}