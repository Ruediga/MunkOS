#pragma once

#include <stdint.h>

#include "cpu_id.h"
#include "pmm.h"
#include "acpi.h"

// ioapic
// ============================================================================
void init_ioapic(void);
void ioapic_redirect_irq(uint32_t irq, uint32_t vector, uint32_t lapic_id);
void ioapic_write(struct acpi_ioapic *ioapic, uint32_t reg, uint32_t val);
uint32_t ioapic_read(struct acpi_ioapic *ioapic, uint32_t reg);

// lapic
// ============================================================================
extern struct cpuid_data_common cpuid_data;
extern uintptr_t lapic_address;

void init_lapic(void);
void lapic_write(uint32_t reg, uint32_t val);
uint32_t lapic_read(uint32_t reg);
void lapic_send_eoi_signal(void);