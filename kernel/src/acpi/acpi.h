#pragma once

#include "limine.h"
#include "acpi/tables.h"

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