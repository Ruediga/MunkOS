#pragma once

#include "vector.h"

#include <stdint.h>

typedef struct acpi_mcfg_entry mcfg_entry;
VECTOR_DECL_TYPE_NON_NATIVE(mcfg_entry);

// wrapper to represent a device plugged into the pci(e) bus
struct pci_device {
    uintptr_t phys_base;

    uint8_t bus;
    uint8_t dev_slot;
    uint8_t function;

    uint16_t segment;

    uint16_t dev_id;
    uint16_t vendor_id;

    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t prog_if;
    uint8_t revision_id;
};

typedef struct pci_device * pci_device_ptr;
VECTOR_DECL_TYPE(pci_device_ptr)

void init_pci(void);