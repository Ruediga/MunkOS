#include "pci.h"
#include "io.h"
#include "acpi.h"
#include "interrupt.h"
#include "kprintf.h"
#include "pmm.h"
#include "vmm.h"

VECTOR_TMPL_TYPE_NON_NATIVE(mcfg_entry);
VECTOR_TMPL_TYPE(pci_device_ptr)

static vector_mcfg_entry_t mcfg_entries = VECTOR_INIT(mcfg_entry);
static vector_pci_device_ptr_t pci_devices = VECTOR_INIT(pci_device_ptr);

enum pci_rw_size {
    BYTE = 1,
    WORD = 2,
    DOUBLE_WORD = 4
};

// returns reg of dev using mcfg
uint32_t pci_read(struct pci_device *dev, uint32_t reg, enum pci_rw_size size)
{
    uintptr_t dev_virt = dev->phys_base + reg + hhdm->offset;

    if (size == BYTE) {
        return (uint32_t)(*((uint8_t *)dev_virt));
    } else if (size == WORD) {
        return (uint32_t)(*((uint16_t *)dev_virt));
    } else if (size == DOUBLE_WORD) {
        return (uint32_t)(*((uint32_t *)dev_virt));
    }
    return 0xDEADBEEF;
}

// writes val to reg of dev using mcfg
void pci_write(struct pci_device *dev, uint32_t reg, uint32_t val, enum pci_rw_size size)
{
    uintptr_t dev_virt = dev->phys_base + reg + hhdm->offset;

    if (size == BYTE) {
         *((uint8_t *)dev_virt) = (uint8_t)val;
    } else if (size == WORD) {
        *((uint16_t *)dev_virt) = (uint16_t)val;
    } else if (size == DOUBLE_WORD) {
        *((uint32_t *)dev_virt) = (uint32_t)val;
    }
}

static inline void pci_dev_calc_phys(struct pci_device *dev)
{
    for (size_t i = 0; i < mcfg_entries.get_size(&mcfg_entries); i++) {
        mcfg_entry *entry = &mcfg_entries.data[i];
        if (dev->bus < entry->host_start || entry->host_end < dev->bus ) {
            continue;
        }
        dev->segment = entry->segment; // [TODO] unnecessary?

        dev->phys_base = entry->base + (((dev->bus - entry->host_start) << 20) | (dev->dev_slot << 15) | (dev->function << 12));

        vmm_map_single_page(&kernel_pmc, ALIGN_DOWN(dev->phys_base + hhdm->offset, PAGE_SIZE), ALIGN_DOWN(dev->phys_base, PAGE_SIZE),
            PTE_BIT_EXECUTE_DISABLE | PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
        return;
    }

    kpanic(NULL, "failed to find physical address of pci device\n");
}

void pci_scan(uint8_t bus, uint8_t dev_slot, uint8_t function)
{
    struct pci_device *device = kmalloc(sizeof(struct pci_device));

    device->bus = bus;
    device->dev_slot = dev_slot;
    device->function = function;

    pci_dev_calc_phys(device);

    // if no device
    if (pci_read(device, 0x0, DOUBLE_WORD) == 0xFFFFFFFF) {
        kfree(device);
        return;
    }

    uint32_t reg0 = pci_read(device, 0x0, DOUBLE_WORD);
    device->vendor_id = (uint16_t)(reg0);
    device->dev_id = (uint16_t)(reg0 >> 16);

    uint32_t reg2 = pci_read(device, 0x8, DOUBLE_WORD);
    device->revision_id = (uint8_t)(reg2);
    device->prog_if = (uint8_t)(reg2 >> 8);
    device->subclass_code = (uint8_t)(reg2 >> 16);
    device->class_code = (uint8_t)(reg2 >> 24);

    pci_devices.push_back(&pci_devices, device);
}

void pci_scan_bus(uint8_t bus)
{
    for (uint8_t dev_slot = 0; dev_slot < 32; dev_slot++) {
        for (int function = 0; function < 8; function++) {
            pci_scan(bus, dev_slot, function);
        }
    } 
}

void init_pci(void)
{
    if (!mcfg_ptr) kpanic(NULL, "PCIe not available, PCI as fallback not supported\n");

    size_t count = (mcfg_ptr->header.length - sizeof(struct acpi_sdt_header) - sizeof(uint64_t)) / sizeof(struct acpi_mcfg_entry);
    if (!count) kpanic(NULL, "PCIe not available, PCI as fallback not supported\n");

    for (size_t i = 0; i < count; i++) {
        kprintf("  - pci: found segment %u (PCIe bus: start = %u; end = %u)\n",
            (uint32_t)mcfg_ptr->entries->segment, (uint32_t)mcfg_ptr->entries[i].host_start, (uint32_t)mcfg_ptr->entries[i].host_end);
        mcfg_entries.push_back(&mcfg_entries, mcfg_ptr->entries + i);
    }

    // scan all buses
    struct pci_device root_bus = { 0 };
    pci_dev_calc_phys(&root_bus);

    uint32_t root_bus_reg_0x2 = pci_read(&root_bus, 0xC, DOUBLE_WORD);

    if (!(root_bus_reg_0x2 & (1u << 23))) {
        // 1 PCI host controller
        pci_scan_bus(0);
    } else {
        // multiple PCI host controllers
        for (uint8_t i = 0; i < 8; i++) {
            root_bus.function = i;
            pci_dev_calc_phys(&root_bus);

            if (pci_read(&root_bus, 0x0, WORD) == 0xFFFF) continue;

            pci_scan_bus(i);
        }
    }
//<bus>:<device>.<function> Class: <class> Vendor: <vendor> Device: <device> ...

    kprintf("  - pci: devices found:\n");
    for (size_t i = 0; i < pci_devices.size; i++) {
        struct pci_device *dev = pci_devices.data[i];
        kprintf("  - %02u:%0u:%0u %0x:%0x %0u:%0u:%0u\n",
            (uint32_t)dev->bus, (uint32_t)dev->dev_slot, (uint32_t)dev->function,
            (uint32_t)dev->vendor_id, (uint32_t)dev->dev_id,
            (uint32_t)dev->class_code, (uint32_t)dev->subclass_code, (uint32_t)dev->prog_if);
    }
}