#pragma once

void parseACPI(void);
void *sdtFind(const char signature[static 4]);

// root system description pointer (pa)
extern struct rsdp *rsdp_ptr;
// root / extended system description table (pa)
extern struct rsdt *rsdt_ptr;

extern struct fadt *fadt_ptr;
extern struct madt *madt_ptr;