#include <cpuid.h>

#include "cpu_id.h"
#include "kheap.h"
#include "kprintf.h"
#include "memory.h"
#include "interrupt.h"
#include "compiler.h"

comp_no_asan inline void cpuid(struct cpuid_ctx *ctx) {
    __asm__ volatile (
        "cpuid"
        : "=a" (ctx->eax), "=b" (ctx->ebx), "=c" (ctx->ecx), "=d" (ctx->edx)
        : "a" (ctx->leaf)
        :
    );
}

// 13 chars (including \0)
comp_no_asan static void cpuid_leaf0x0(struct cpuid_data_common *data)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    // leaf 0: vendor
    __get_cpuid(0x0, &eax, &ebx, &ecx, &edx);

    data->highest_supported_std_func = eax;

    *((uint32_t *)(data->cpu_vendor)) = ebx;
    *((uint32_t *)(data->cpu_vendor + 4)) = edx;
    *((uint32_t *)(data->cpu_vendor + 8)) = ecx;
    *(data->cpu_vendor + 12) = '\0';
}

comp_no_asan static void cpuid_leaf0x1(struct cpuid_data_common *data)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    // leaf 1: processor, misc and feature flags
    __get_cpuid(0x1, &eax, &ebx, &ecx, &edx);

    uint32_t family = ((eax >> 8) & 0xFu) | (((eax >> 20) & 0xFu) << 4u);
    uint32_t model = ((eax >> 4) & 0xFu) | (((eax >> 16) & 0xFu) << 4u);
    uint32_t stepping = eax & 0xFu;

    data->family = (uint16_t)family;
    data->model = (uint8_t)model;
    data->stepping = (uint8_t)stepping;

    data->brand_id = (uint8_t)(ebx & 0xFu);
    data->clflush_size = (uint8_t)((ebx >> 8u) & 0xFu);
    data->cpu_count = (uint8_t)((ebx >> 8u) & 0xFu);
    data->apic_id = (uint8_t)((ebx >> 8u) & 0xFu);

    data->feature_flags_ecx = ecx;
    data->feature_flags_edx = edx;
}

comp_no_asan static void cpuid_leaf_0x80000002(struct cpuid_data_common *data)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    // leaf 0x80000002: cpu name string char 0 - 15
    __get_cpuid(0x80000002, &eax, &ebx, &ecx, &edx);

    *((uint32_t *)(data->cpu_name_string + 0)) = eax;
    *((uint32_t *)(data->cpu_name_string + 4)) = ebx;
    *((uint32_t *)(data->cpu_name_string + 8)) = ecx;
    *((uint32_t *)(data->cpu_name_string + 12)) = edx;
}

comp_no_asan static void cpuid_leaf_0x80000003(struct cpuid_data_common *data)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    // leaf 0x80000003: cpu name string char 16 - 31
    __get_cpuid(0x80000003, &eax, &ebx, &ecx, &edx);

    *((uint32_t *)(data->cpu_name_string + 16)) = eax;
    *((uint32_t *)(data->cpu_name_string + 20)) = ebx;
    *((uint32_t *)(data->cpu_name_string + 24)) = ecx;
    *((uint32_t *)(data->cpu_name_string + 28)) = edx;
}

comp_no_asan static void cpuid_leaf_0x80000004(struct cpuid_data_common *data)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    // leaf 0x80000004: cpu name string char 32 - 47
    __get_cpuid(0x80000004, &eax, &ebx, &ecx, &edx);

    *((uint32_t *)(data->cpu_name_string + 32)) = eax;
    *((uint32_t *)(data->cpu_name_string + 36)) = ebx;
    *((uint32_t *)(data->cpu_name_string + 40)) = ecx;
    *((uint32_t *)(data->cpu_name_string + 44)) = edx;

    *(data->cpu_name_string + 48) = '\0';
}

comp_no_asan void cpuid_common(struct cpuid_data_common *data)
{
    cpuid_leaf0x0(data);
    cpuid_leaf0x1(data);
    if (!memcmp(data->cpu_vendor, "GenuineIntel", 13)) {
        // intel only
        cpuid_leaf_0x80000002(data);
        cpuid_leaf_0x80000003(data);
        cpuid_leaf_0x80000004(data);
    } else {
        memset(data->cpu_name_string, 0, 49);
    }
}

comp_no_asan void cpuid_compatibility_check(struct cpuid_data_common *data)
{
    uint8_t okay = 1;

    // x2APIC
    //if (!(data->feature_flags_ecx & (1 << 21))) okay = 0;
    // SSE4.2
    //if (!(data->feature_flags_ecx & (1 << 20))) okay = 0;

    // smp
    if (!(data->feature_flags_edx & (1 << 28))) okay = 0;
    // FXSAVE and FXRSTOR
    if (!(data->feature_flags_edx & (1 << 24))) okay = 0;
    // processor monitoring
    //if (!(data->feature_flags_edx & (1 << 22))) okay = 0;
    // SYSENTER / SYSEXIT
    if (!(data->feature_flags_edx & (1 << 11))) okay = 0;
    // has a lapic
    if (!(data->feature_flags_edx & (1 << 9))) okay = 0;
    // > 32bit PA
    if (!(data->feature_flags_edx & (1 << 6))) okay = 0;
    // x87 FPU
    if (!(data->feature_flags_edx & (1 << 0))) okay = 0;

    if (!okay) {
        kpanic(0, NULL, "\nYour CPU doesn't support required hardware features.\n");
    }
}