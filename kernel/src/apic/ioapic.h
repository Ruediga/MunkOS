#pragma once

#include <stdint.h>
#include "acpi/structures.h"

void initIoapic(void);
void ioapic_redirect_irq(uint32_t irq, uint32_t vector, uint32_t lapic_id);
void ioapic_write(struct acpi_ioapic *ioapic, uint32_t reg, uint32_t val);
uint32_t ioapic_read(struct acpi_ioapic *ioapic, uint32_t reg);