#pragma once

#include "cpu/cpuid.h"
#include "mm/pmm.h"

#include <stdint.h>

extern struct cpuid_data_common cpuid_data;

void init_lapic(void);
void lapic_write(uint32_t reg, uint32_t val);
uint32_t lapic_read(uint32_t reg);
void lapic_send_eoi_signal(void);