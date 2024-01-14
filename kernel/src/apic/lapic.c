#include "apic/lapic.h"
#include "std/kprintf.h"
#include "cpu/io.h"
#include "mm/vmm.h"

// sdm vol3 ch 11.4

#define LAPIC_SPURIOUS_INT_VEC_REG 0x0F0
#define LAPIC_EOI_REG 0x0B0

static uintptr_t lapic_address = 0;

inline uint32_t lapic_read(uint32_t reg)
{
    return *((volatile uint32_t *)(lapic_address + reg));
}

inline void lapic_write(uint32_t reg, uint32_t val)
{
    *((volatile uint32_t *)(lapic_address + reg)) = val;
}

void initLapic(void)
{
    // [TODO] move this to compat check
    if (!(cpuid_data.feature_flags_edx & (1 << 9))) {
        kprintf("you don't have a lapic, get fucked\n");
        asm volatile("cli\n hlt");
    }

    lapic_address = (read_msr(0x1b) & 0xfffff000);
    mapPage(&kernel_pmc, ALIGN_DOWN((lapic_address + hhdm->offset), PAGE_SIZE),
        ALIGN_DOWN(lapic_address, PAGE_SIZE), PTE_BIT_PRESENT | PTE_BIT_READ_WRITE);
    lapic_address += hhdm->offset;

    // enable APIC (1 << 8), spurious vector = 0xFF
    lapic_write(LAPIC_SPURIOUS_INT_VEC_REG, lapic_read(LAPIC_SPURIOUS_INT_VEC_REG) | 0x100 | 0xFF);
}

inline void lapic_send_eoi_signal(void)
{
    // non zero value may cause #GP
    lapic_write(LAPIC_EOI_REG, 0x00);
}