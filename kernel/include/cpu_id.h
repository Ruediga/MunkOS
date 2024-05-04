#pragma once

#include <stdint.h>

typedef uint64_t qword;
typedef uint32_t dword;
typedef uint16_t word;
typedef uint8_t byte;

struct cpuid_ctx {
    dword leaf;
    dword eax;
    dword ebx;
    dword ecx;
    dword edx;
};

// https://sandpile.org/x86/cpuid.htm
struct cpuid_data_common {
    uint32_t highest_supported_std_func;
    char cpu_vendor[13]; // null terminated
    uint16_t family; // actual size 12 bits
    uint8_t model; // actual size 8 bits
    uint8_t stepping; // actual size 4 bits
    uint8_t apic_id; // only if apic_id flag
    uint8_t cpu_count; // only if hyper-threading flag
    uint8_t clflush_size; // only if cflush flag
    uint8_t brand_id;
    uint32_t feature_flags_ecx;
    uint32_t feature_flags_edx;
    // null terminated, for non-intel cpus, this is zeroed out
    char cpu_name_string[49];
};

void cpuid_common(struct cpuid_data_common *data);
void cpuid_compatibility_check(struct cpuid_data_common *data);

void cpuid(struct cpuid_ctx *ctx);