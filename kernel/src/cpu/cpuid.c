#include <cpuid.h>

#include "dynmem/liballoc.h"
#include "std/kprintf.h"

void cpuid_getCpuVendor(char str[static 13])
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    // leaf 0: vendor
    __get_cpuid(0x0, &eax, &ebx, &ecx, &edx);

    *(uint32_t *)(str) = ebx;
    *(uint32_t *)(str + 4) = edx;
    *(uint32_t *)(str + 8) = ecx;
    str[12] = '\0';
}