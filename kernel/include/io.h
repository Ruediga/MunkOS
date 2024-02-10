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

inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

inline uint16_t inw(uint16_t port)
{
    uint16_t out;
    __asm__ volatile("inw %1, %0" : "=a"(out) : "Nd"(port) : "memory");
    return out;
}

inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

inline uint32_t inl(uint16_t port)
{
    uint32_t out;
    __asm__ volatile("inl %1, %0" : "=a"(out) : "Nd"(port) : "memory");
    return out;
}