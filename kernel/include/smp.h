#pragma once

#include "limine.h"

#include <stddef.h>
#include <stdint.h>

extern struct limine_smp_request smp_request;

extern struct limine_smp_response *smp_response;
extern struct smp_cpu *global_cpus;
extern uint64_t smp_cpu_count;

void boot_other_cores(void);

struct smp_cpu {
    size_t id; // core id
    uint32_t lapic_id; // lapic id of the processor
};