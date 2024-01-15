#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "limine.h"
#include "acpi/structures.h"

void parse_acpi(void);
void *get_sdt(const char signature[static 4]);
void parse_madt(struct acpi_madt *_madt);

// root system description pointer (pa)
extern struct acpi_rsdp *rsdp_ptr;
// root / extended system description table (pa)
extern struct acpi_rsdt *rsdt_ptr;

extern struct acpi_fadt *fadt_ptr;
extern struct acpi_madt *madt_ptr;

extern struct limine_rsdp_request rsdp_request;

extern struct acpi_ioapic *ioapics;
extern struct acpi_lapic *lapics;
extern struct acpi_iso *isos;

extern size_t ioapic_count;
extern size_t lapic_count;
extern size_t iso_count;