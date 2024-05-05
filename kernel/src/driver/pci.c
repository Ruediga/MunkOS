#include "pci.h"
#include "io.h"
#include "_acpi.h"
#include "interrupt.h"
#include "kprintf.h"
#include "frame_alloc.h"
#include "vmm.h"
#include "nvme.h"

VECTOR_TMPL_TYPE_NON_NATIVE(mcfg_entry);
VECTOR_TMPL_TYPE(pci_device_ptr)

static vector_mcfg_entry_t mcfg_entries = VECTOR_INIT(mcfg_entry);
static vector_pci_device_ptr_t pci_devices = VECTOR_INIT(pci_device_ptr);

// https://pcisig.com/sites/default/files/files/PCI_Code-ID_r_1_11__v24_Jan_2019.pdf
// https://pci-ids.ucw.cz/

static struct pci_dev_classifier pci_dev_base_classes[] = {
    {0x0, "Unclassified"},
    {0x1, "Mass storage controller"},
    {0x2, "Network controller"},
    {0x3, "Display controller"},
    {0x4, "Multimedia device"},
    {0x5, "Memory controller"},
    {0x6, "Bridge device"},
    {0x7, "Simple communication controllers"},
    {0x8, "Base system peripheral"},
    {0x9, "Input devices"},
    {0xA, "Docking stations"},
    {0xB, "Processors"},
    {0xC, "Serial bus controllers"},
    {0xD, "Wireless controller"},
    {0xE, "Intelligent I/O controllers"},
    {0xF, "Satellite communication controllers"},
    {0x10, "Encryptions/Decryption controllers"},
    {0x11, "Data acquisition and signal processing controllers"},
    {0x12, "Processing accelerators"},
    {0x13, "Non-Essential Instrumentation"},
    {0xFF, "Device doesn't fit any defined class"}
};

static char *pci_get_dev_class_string(pci_device *dev)
{
    for (size_t i = 0; i < sizeof(pci_dev_base_classes) / sizeof(struct pci_dev_classifier); i++) {
        struct pci_dev_classifier *classifier = &pci_dev_base_classes[i]; 
        // base class
        if (dev->class_code == classifier->code) {
            return classifier->string;
        }
    }
    return "unkown / invalid device class";
}

// returns reg of dev using mcfg
uint32_t pci_read(pci_device *dev, uint32_t reg, enum pci_rw_size size)
{
    uintptr_t dev_virt = dev->phys_base + reg + hhdm->offset;

    if (size == BYTE) {
        return (uint32_t)(*((volatile uint8_t *)dev_virt));
    } else if (size == WORD) {
        return (uint32_t)(*((volatile uint16_t *)dev_virt));
    } else if (size == DOUBLE_WORD) {
        return (uint32_t)(*((volatile uint32_t *)dev_virt));
    }
    return 0xDEADBEEF;
}

// writes val to reg of dev using mcfg
void pci_write(pci_device *dev, uint32_t reg, uint32_t val, enum pci_rw_size size)
{
    uintptr_t dev_virt = dev->phys_base + reg + hhdm->offset;

    if (size == BYTE) {
         *((volatile uint8_t *)dev_virt) = (uint8_t)val;
    } else if (size == WORD) {
        *((volatile uint16_t *)dev_virt) = (uint16_t)val;
    } else if (size == DOUBLE_WORD) {
        *((volatile uint32_t *)dev_virt) = (uint32_t)val;
    }
}

static inline void pci_dev_calc_phys(pci_device *dev)
{
    for (size_t i = 0; i < mcfg_entries.size; i++) {
        mcfg_entry *entry = &mcfg_entries.data[i];
        if (dev->bus < entry->host_start || entry->host_end < dev->bus ) {
            continue;
        }

        dev->phys_base = entry->base + (((dev->bus - entry->host_start) << 20) | (dev->dev_slot << 15) | (dev->function << 12));

        vmm_map_single_page(&kernel_pmc, ALIGN_DOWN(dev->phys_base + hhdm->offset, PAGE_SIZE), ALIGN_DOWN(dev->phys_base, PAGE_SIZE),
            PTE_BIT_EXECUTE_DISABLE | PTE_BIT_PRESENT | PTE_BIT_READ_WRITE | PTE_BIT_DISABLE_CACHING);
        return;
    }

    kpanic(0, NULL, "failed to find physical address of pci device\n");
}

void pci_check_device(uint8_t bus, uint8_t dev_slot, uint8_t function)
{
    pci_device *device = kmalloc(sizeof(pci_device));

    device->bus = bus;
    device->dev_slot = dev_slot;
    device->function = function;

    pci_dev_calc_phys(device);

    // if no device
    if (pci_read(device, PCI_HEADER_REG0x0, DOUBLE_WORD) == 0xFFFFFFFF) {
        kfree(device);
        return;
    }

    uint32_t reg0 = pci_read(device, PCI_HEADER_REG0x0, DOUBLE_WORD);
    device->vendor_id = (uint16_t)(reg0);
    device->dev_id = (uint16_t)(reg0 >> 16);

    uint32_t reg2 = pci_read(device, PCI_HEADER_REG0x2, DOUBLE_WORD);
    device->revision_id = (uint8_t)(reg2);
    device->prog_if = (uint8_t)(reg2 >> 8);
    device->subclass_code = (uint8_t)(reg2 >> 16);
    device->class_code = (uint8_t)(reg2 >> 24);

    // if pci bridge:
    if (device->class_code == 0x6 && device->subclass_code == 0x4) {
        kprintf("found pci to pci bridge with prog_if=%u: ", (uint32_t)device->prog_if);
        uint32_t bus_regs = pci_read(device, 0x18, DOUBLE_WORD);
        kprintf("primary=%u, secondary=%u, subordinate=%u\n",
        bus_regs & 0xFF, (bus_regs >> 8) & 0xFF, (bus_regs >> 16) & 0xFF);
        pci_scan_bus((bus_regs >> 8) & 0xFF);
    }

    pci_devices.push_back(&pci_devices, device);
}

void pci_scan_bus(uint8_t bus)
{
    for (uint8_t dev_slot = 0; dev_slot < 32; dev_slot++) {
        for (int function = 0; function < 8; function++) {
            pci_check_device(bus, dev_slot, function);
        }
    } 
}

// [TODO] handle pci-to-pci bridge adapters
void init_pci(void)
{
    if (!mcfg_ptr) kpanic(0, NULL, "PCIe not available, PCI as fallback not supported\n");

    size_t count = (mcfg_ptr->header.length - sizeof(struct acpi_sdt_header) - sizeof(uint64_t)) / sizeof(struct acpi_mcfg_entry);
    if (!count) kpanic(0, NULL, "PCIe not available, PCI as fallback not supported\n");

    for (size_t i = 0; i < count; i++) {
        kprintf("  - pci: found segment %u (PCIe bus: start = %u; end = %u)\n",
            (uint32_t)mcfg_ptr->entries->segment, (uint32_t)mcfg_ptr->entries[i].host_start, (uint32_t)mcfg_ptr->entries[i].host_end);
        mcfg_entries.push_back(&mcfg_entries, (struct acpi_mcfg_entry *)mcfg_ptr->entries + i);
    }

    // scan all buses
    pci_device root_bus = { 0 };
    pci_dev_calc_phys(&root_bus);

    uint32_t root_bus_reg_0x3 = pci_read(&root_bus, PCI_HEADER_REG0x3, DOUBLE_WORD);

    if (!(root_bus_reg_0x3 & (1u << 23))) {
        // 1 PCI host controller
        pci_scan_bus(0);
    } else {
        // multiple PCI host controllers
        for (uint8_t i = 0; i < 8; i++) {
            root_bus.function = i;
            pci_dev_calc_phys(&root_bus);

            if (pci_read(&root_bus, PCI_HEADER_REG0x0, WORD) == 0xFFFF) continue;

            pci_scan_bus(i);
        }
    }

    kprintf("  - pci: %lu devices connected [class:subclass:prog_if -> vendor_id:dev_id @ bus:dev:func -> class_string]:\n", pci_devices.size);
    for (size_t i = 0; i < pci_devices.size; i++) {
        pci_device *dev = pci_devices.data[i];
        kprintf("  - dev %lu: %02x:%02x:%02x -> %04x:%04x @ %02x:%02x:%02x -> %s\n", i,
            (uint32_t)dev->class_code, (uint32_t)dev->subclass_code, (uint32_t)dev->prog_if,
            (uint32_t)dev->vendor_id, (uint32_t)dev->dev_id,
            (uint32_t)dev->bus, (uint32_t)dev->dev_slot, (uint32_t)dev->function,
            pci_get_dev_class_string(dev));

        // init drivers
        if (dev->class_code == 0x1 && dev->subclass_code == 0x8 && dev->prog_if == 0x2) {
            kprintf("  - pci: initializing nvme driver...\n\r");
            init_nvme_controller(dev);
        }
    }
}

// pass *zeroed* pci_base_addr_reg_ctx structure
void pci_read_bar(pci_device *dev, struct pci_base_addr_reg_ctx *bar_ctx, int bar_idx)
{
    // disable memory and io response
    uint32_t cmd_reg = pci_read(dev, PCI_HEADER_REG0x1, WORD);
    pci_write(dev, PCI_HEADER_REG0x1, cmd_reg & (~0b11), WORD);

    size_t reg_off = PCI_HEADER_REG0x4 + bar_idx * 0x4;

    // base + size
    uint64_t base = (uint64_t)pci_read(dev, reg_off, DOUBLE_WORD);
    pci_write(dev, reg_off, 0xFFFFFFFF, DOUBLE_WORD);
    uint32_t size = pci_read(dev, reg_off, DOUBLE_WORD);
    pci_write(dev, reg_off, base, DOUBLE_WORD);

    if (base & 1) {
        // not mmio

        bar_ctx->base = (void *)(base & (~0b11ul));
        bar_ctx->size = ~(size & (~0b11u)) + 1;
    } else {
        // mmio
        bar_ctx->is_mmio_bar = true;

        // prefetchable?
        if ((base >> 3) & 1) {
            bar_ctx->is_prefetchable = true;
        }

        // 64 bit wide?
        if (((base >> 1) & 0b11) == 0x2) {
            base |= (uint64_t)pci_read(dev, reg_off + 0x4, DOUBLE_WORD) << 32ul;
        }

        bar_ctx->base = (void *)(base & (~0b1111ul));
        bar_ctx->size = ~(size & (~0b1111u)) + 1;
    }

    pci_write(dev, PCI_HEADER_REG0x1, cmd_reg, WORD);
}

// ZEROES OUT the reg and then sets all flags
void pci_set_command_reg(pci_device *dev, uint16_t flags)
{
    pci_write(dev, PCI_HEADER_REG0x1, flags, WORD);
}