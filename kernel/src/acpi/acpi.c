#include "std/kprintf.h"
#include "std/memory.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "acpi.h"
#include "dynmem/vector.h"

bool xsdt_present;

// root system description pointer (pa)
struct rsdp *rsdp_ptr = NULL;
// root / extended system description table (pa)
struct rsdt *rsdt_ptr = NULL;

struct fadt *fadt_ptr = NULL;
struct madt *madt_ptr = NULL;

struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

static vector vec_ioapic;
static vector vec_lapic;

// these need to hold addresses of vector->data!
struct ioapic *ioapics;
struct lapic *lapics;

size_t ioapic_count = 0;
size_t lapic_count = 0;

static bool validate_table(struct sdt_header *table_header)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < table_header->length; i++) {
        sum += ((uint8_t *)table_header)[i];
    }

    return (sum == 0);
}

// [FIXME] fix this shitty stuff with the mapping
void parseACPI(void)
{
    if (rsdp_request.response == NULL || rsdp_request.response->address == NULL) {
        kprintf("ACPI is not supported\n");
        asm volatile("cli\n hlt");
    }

    rsdp_ptr = rsdp_request.response->address;
    mapPage(&kernel_pmc, ALIGN_DOWN((uintptr_t)rsdp_ptr, PAGE_SIZE),
        ALIGN_DOWN(((uintptr_t)rsdp_ptr - hhdm->offset), PAGE_SIZE), PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);

    // use xsdt for newer revisions
    xsdt_present = (rsdp_ptr->revision >= 2) ? true : false;
    rsdt_ptr = xsdt_present ? (struct rsdt *)((uintptr_t)rsdp_ptr->xsdt_address + hhdm->offset)
        : (struct rsdt *)((uintptr_t)rsdp_ptr->rsdt_address + hhdm->offset);
    if (rsdt_ptr == NULL) {
        kprintf("ACPI is not supported\n");
        asm volatile("cli\n hlt");
    }
    mapPage(&kernel_pmc, ALIGN_DOWN((uintptr_t)rsdt_ptr, PAGE_SIZE),
        ALIGN_DOWN(((uintptr_t)rsdt_ptr - hhdm->offset), PAGE_SIZE), PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);

    // temp solution [FIXME] to map acpi tables
    size_t entry_count = (rsdt_ptr->header.length - sizeof(struct sdt_header)) / (xsdt_present ? 8 : 4);
    for (size_t i = 0; i < entry_count; i++) {
        struct sdt_header *head = NULL;
        head = (struct sdt_header *)(xsdt_present ? (((uint64_t *)(rsdt_ptr->ptr))[i] + hhdm->offset)
            : ((((uint32_t *)(rsdt_ptr->ptr))[i]) + hhdm->offset));
        mapPage(&kernel_pmc, ALIGN_DOWN((uintptr_t)head, PAGE_SIZE),
            ALIGN_DOWN(((uintptr_t)head - hhdm->offset), PAGE_SIZE), PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
    }

    if (!(fadt_ptr = sdtFind("FACP")) || !validate_table(&fadt_ptr->header)) {
        kprintf("FADT not found\n");
        asm volatile("cli\n hlt");
    }
    if (!(madt_ptr = sdtFind("APIC")) || !validate_table(&madt_ptr->header)) {
        kprintf("MADT not found\n");
        asm volatile("cli\n hlt");
    }

    vector_init(&vec_lapic);
    vector_init(&vec_ioapic);
    parseMADT(madt_ptr);
    lapics = (struct lapic *)vec_lapic.data;
    ioapics = (struct ioapic *)vec_ioapic.data;

}

void *sdtFind(const char signature[static 4])
{
    // length = bytes of the entire table
    size_t entry_count = (rsdt_ptr->header.length - sizeof(struct sdt_header)) / (xsdt_present ? 8 : 4);

    for (size_t i = 0; i < entry_count; i++) {
        struct sdt_header *head = (xsdt_present ?
            (struct sdt_header *)(((uint64_t *)rsdt_ptr->ptr)[i] + hhdm->offset)
            : (struct sdt_header *)(((uint32_t *)rsdt_ptr->ptr)[i] + hhdm->offset));

        if (!memcmp(head->signature, signature, 4)) {
            kprintf("  - acpi: %4s found at (pa) 0x%p of length %-u\n",
                signature, (uintptr_t)head - hhdm->offset, head->length);
            return head;
        }
    }

    return NULL;
}

void parseMADT(struct madt *_madt)
{
    for (uintptr_t off = 0; off < _madt->header.length - sizeof(struct madt); ) {
        struct madt_header *madt_hdr = (struct madt_header *)(_madt->entries + off);
        if (madt_hdr->lcst_id == MADT_ENTRY_PROCESSOR_LAPIC) {
            // lapic
            vector_append(&vec_lapic, madt_hdr, madt_hdr->length);
            lapic_count++;
        }
        else if (madt_hdr->lcst_id == MADT_ENTRY_IO_APIC) {
            // ioapic
            vector_append(&vec_ioapic, madt_hdr, madt_hdr->length);
            ioapic_count++;
        }

        off += MAX(madt_hdr->length, 2);
    }
    kprintf("  - acpi: found %lu ioapic(s) and %lu lapic(s)\n", ioapic_count, lapic_count);
    if (ioapic_count == 0) {
        kprintf("Systems without an IOAPIC are not supported!\n");
        asm volatile ("cli\n hlt");
    }
}