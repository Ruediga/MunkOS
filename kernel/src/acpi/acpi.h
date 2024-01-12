#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "limine.h"
#include "acpi/structures.h"

void parseACPI(void);
void *sdtFind(const char signature[static 4]);
void parseMADT(struct madt *_madt);

// root system description pointer (pa)
extern struct rsdp *rsdp_ptr;
// root / extended system description table (pa)
extern struct rsdt *rsdt_ptr;

extern struct fadt *fadt_ptr;
extern struct madt *madt_ptr;

extern struct limine_rsdp_request rsdp_request;

extern struct ioapic *ioapics;
extern struct lapic *lapics;

extern size_t ioapic_count;
extern size_t lapic_count;