#pragma once

#include "vector.h"

#include <stdint.h>
#include <stdbool.h>

// common header
#define PCI_HEADER_REG0x0 0x0
#define PCI_HEADER_REG0x1 0x4
#define PCI_HEADER_REG0x2 0x8
#define PCI_HEADER_REG0x3 0xC
// header type 0x0 and 0x1 bar0
#define PCI_HEADER_REG0x4 0x10

// command register flags
#define PCI_CMD_REG_FLAG_MMIO (1)
#define PCI_CMD_REG_FLAG_MEMORY_SPACE (1 << 1)
#define PCI_CMD_REG_FLAG_BUS_MASTER (1 << 2)
#define PCI_CMD_REG_FLAG_SPECIAL_CYCLES (1 << 3)
#define PCI_CMD_REG_FLAG_WR_INVAL_ENABLE (1 << 4)
#define PCI_CMD_REG_FLAG_VGA_PAL_SNOOP (1 << 5)
#define PCI_CMD_REG_FLAG_PARITY_ERR_RESP (1 << 6)
#define PCI_CMD_REG_FLAG_SERR_ENABLE (1 << 7)
#define PCI_CMD_REG_FLAG_FAST_BTB_ENABLE (1 << 9)
#define PCI_CMD_REG_FLAG_INTERRUPT_DISABLE (1 << 10)

typedef struct acpi_mcfg_entry mcfg_entry;
VECTOR_DECL_TYPE_NON_NATIVE(mcfg_entry);

// wrapper to represent a device plugged into the pci(e) bus
typedef struct pci_device {
    uintptr_t phys_base;

    uint8_t bus;
    uint8_t dev_slot;
    uint8_t function;

    uint16_t dev_id;
    uint16_t vendor_id;

    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t prog_if;
    uint8_t revision_id;
} pci_device;

struct pci_base_addr_reg_ctx {
    void *base;
    size_t size;
    bool is_mmio_bar;       // mmio | ram (1|0)
    bool is_prefetchable;   // map as WC instead of UC (only for memory space bars)
};

struct pci_dev_classifier {
    uint8_t code;
    char *string;
};

enum pci_rw_size {
    BYTE = 1,
    WORD = 2,
    DOUBLE_WORD = 4
};

typedef pci_device * pci_device_ptr;
VECTOR_DECL_TYPE(pci_device_ptr)

void init_pci(void);
uint32_t pci_read(pci_device *dev, uint32_t reg, enum pci_rw_size size);
void pci_write(pci_device *dev, uint32_t reg, uint32_t val, enum pci_rw_size size);
void pci_check_device(uint8_t bus, uint8_t dev_slot, uint8_t function);
void pci_scan_bus(uint8_t bus);
void pci_read_bar(pci_device *dev, struct pci_base_addr_reg_ctx *bar_ctx, int bar_idx);
void pci_set_command_reg(pci_device *dev, uint16_t flags);