#include "kprintf.h"
#include "macros.h"
#include "frame_alloc.h"
#include "mmu.h"
#include "_acpi.h"
#include "interrupt.h"

VECTOR_TMPL_TYPE(acpi_ioapic_ptr)
VECTOR_TMPL_TYPE(acpi_lapic_ptr)
VECTOR_TMPL_TYPE(acpi_iso_ptr)

bool xsdt_present;

// root system description pointer (pa)
volatile struct acpi_rsdp *rsdp_ptr = NULL;
// root / extended system description table (pa)
volatile struct acpi_rsdt *rsdt_ptr = NULL;

volatile struct acpi_fadt *fadt_ptr = NULL;
volatile struct acpi_madt *madt_ptr = NULL;
volatile struct acpi_mcfg *mcfg_ptr = NULL;

struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

vector_acpi_ioapic_ptr_t ioapics = VECTOR_INIT(acpi_ioapic_ptr);
vector_acpi_lapic_ptr_t lapics = VECTOR_INIT(acpi_lapic_ptr);
vector_acpi_iso_ptr_t isos = VECTOR_INIT(acpi_iso_ptr);

static bool validate_table(volatile struct acpi_sdt_header *table_header)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < table_header->length; i++) {
        sum += ((uint8_t *)table_header)[i];
    }

    return (sum == 0);
}

// [FIXME] fix this shitty stuff with the mapping
void parse_acpi(void)
{
    kprintf_verbose("%s parsing acpi tables...\n", ansi_progress_string);

    if (rsdp_request.response == NULL || rsdp_request.response->address == NULL) {
        kpanic(0, NULL, "ACPI is not supported\n");
    }

    rsdp_ptr = rsdp_request.response->address;
    mmu_map_single_page_4k(&kernel_pmc, ALIGN_DOWN((uintptr_t)rsdp_ptr, PAGE_SIZE),
        ALIGN_DOWN(((uintptr_t)rsdp_ptr - hhdm->offset), PAGE_SIZE), PM_COMMON_PRESENT | PM_COMMON_WRITE);

    // use xsdt for newer revisions
    xsdt_present = (rsdp_ptr->revision >= 2) ? true : false;
    rsdt_ptr = xsdt_present ?
        (struct acpi_rsdt *)((uintptr_t)rsdp_ptr->xsdt_address + hhdm->offset)
        : (struct acpi_rsdt *)((uintptr_t)rsdp_ptr->rsdt_address + hhdm->offset);
    if (rsdt_ptr == NULL) {
        kpanic(0, NULL, "ACPI is not supported\n");
    }
    mmu_map_single_page_4k(&kernel_pmc, ALIGN_DOWN((uintptr_t)rsdt_ptr, PAGE_SIZE),
        ALIGN_DOWN(((uintptr_t)rsdt_ptr - hhdm->offset), PAGE_SIZE), PM_COMMON_PRESENT | PM_COMMON_WRITE);

    // temp solution [FIXME] to map acpi tables
    size_t entry_count = (rsdt_ptr->header.length - sizeof(struct acpi_sdt_header)) / (xsdt_present ? 8 : 4);
    for (size_t i = 0; i < entry_count; i++) {
        struct acpi_sdt_header *head = NULL;

        if (xsdt_present) {
            uint32_t *xsdt_table = (uint32_t *)((uintptr_t)rsdt_ptr + sizeof(struct acpi_sdt_header));
            // this may be unnecessary for x86, but since the old alignment is 32 bits,
            // and the new xsdt is 64 bits, this results in an unaligned read otherwise.
            size_t head_lo = xsdt_table[i * 2];
            size_t head_hi = xsdt_table[i * 2 + 1];
            head =  (struct acpi_sdt_header *)(((head_hi << 32) | head_lo) + hhdm->offset);
        } else {    // rsdt
            uint32_t *rsdt_table = (uint32_t *)((uintptr_t)rsdt_ptr + sizeof(struct acpi_sdt_header));
            head = (struct acpi_sdt_header *)(rsdt_table[i] + hhdm->offset);
        }

        mmu_map_single_page_4k(&kernel_pmc, ALIGN_DOWN((uintptr_t)head, PAGE_SIZE),
            ALIGN_DOWN(((uintptr_t)head - hhdm->offset), PAGE_SIZE), PM_COMMON_PRESENT | PM_COMMON_WRITE);
    }

    if (!(fadt_ptr = get_sdt("FACP")) || !validate_table(&fadt_ptr->header)) {
        kpanic(0, NULL, "FADT not found\n");
    }
    if (!(madt_ptr = get_sdt("APIC")) || !validate_table(&madt_ptr->header)) {
        kpanic(0, NULL, "MADT not found\n");
    }
    // PCIe config, removed bcus comp_packed || !validate_table(&mcfg_ptr->header)
    if (!(mcfg_ptr = get_sdt("MCFG"))) {
        kpanic(0, NULL, "MCFG not found\n");
    }

    parse_madt(madt_ptr);

    kprintf("%s parsed necessary acpi tables\n", ansi_okay_string);
}

void *get_sdt(const char signature[static 4])
{
    // length = bytes of the entire table
    size_t entry_count = (rsdt_ptr->header.length - sizeof(struct acpi_sdt_header)) / (xsdt_present ? 8 : 4);

    for (size_t i = 0; i < entry_count; i++) {
        struct acpi_sdt_header *head;

        if (xsdt_present) {
            uint32_t *xsdt_table = (uint32_t *)((uintptr_t)rsdt_ptr + sizeof(struct acpi_sdt_header));
            // this may be unnecessary, but since the old alignment is 32 bits,
            // and the new xsdt is 64 bits, this results in an unaligned read otherwise.
            size_t head_lo = xsdt_table[i * 2];
            size_t head_hi = xsdt_table[i * 2 + 1];
            head = (struct acpi_sdt_header *)(((head_hi << 32) | head_lo) + hhdm->offset);
        } else {    // rsdt
            uint32_t *rsdt_table = (uint32_t *)((uintptr_t)rsdt_ptr + sizeof(struct acpi_sdt_header));
            head = (struct acpi_sdt_header *)(rsdt_table[i] + hhdm->offset);
        }

        if (!memcmp(head->signature, signature, 4)) {
            kprintf("  - acpi: %4s found at (pa) 0x%p of length %-u\n",
                signature, (uintptr_t)head - hhdm->offset, head->length);
            return head;
        }
    }

    return NULL;
}

void parse_madt(volatile struct acpi_madt *madt)
{
    for (uintptr_t off = 0; off < madt->header.length - sizeof(struct acpi_madt); ) {
        struct acpi_madt_header *madt_hdr = (struct acpi_madt_header *)(madt->entries + off);
        if (madt_hdr->lcst_id == MADT_ENTRY_PROCESSOR_LAPIC) {
            // lapic
            lapics.push_back(&lapics, (acpi_lapic_ptr)madt_hdr);
        }
        else if (madt_hdr->lcst_id == MADT_ENTRY_IO_APIC) {
            // ioapic
            ioapics.push_back(&ioapics, (acpi_ioapic_ptr)madt_hdr);
        }
        else if (madt_hdr->lcst_id == MADT_ENTRY_INTERRUPT_SOURCE_OVERRIDE) {
            // iso
            isos.push_back(&isos, (acpi_iso_ptr)madt_hdr);
        }

        off += madt_hdr->length;
    }

    kprintf("  - acpi: found %lu ioapic(s) and %lu lapic(s)\n", ioapics.size, lapics.size);
    if (ioapics.size == 0) {
        kpanic(0, NULL, "Systems without an IOAPIC are not supported!\n");
    }
}