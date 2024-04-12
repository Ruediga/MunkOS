#pragma once

#include <stdint.h>

#include "cpu_id.h"
#include "frame_alloc.h"
#include "acpi.h"
#include "interrupt.h"
#include "vector.h"

#define PIC_MASTER_OFFSET 0x20
#define PIC_SLAVE_OFFSET 0x28

#define LAPIC_SPURIOUS_INT_VEC_REG 0x0F0
#define LAPIC_EOI_REG 0x0B0
#define LAPIC_LVT_CMCI_REG 0x2F0

#define LAPIC_LVT_TIMER_REG 0x320
#define LAPIC_TIMER_LVTTR_ONESHOT (0b00 << 17)
#define LAPIC_TIMER_LVTTR_PERIODIC (0b01 << 17)
#define LAPIC_TIMER_LVTTR_MASKED (0b1 << 16)

#define LAPIC_LVT_THERMAL_MONITOR_REG 0x330
#define LAPIC_LVT_PERF_COUNTER_REG 0x340
#define LAPIC_LVT_LINT0_REG 0x350
#define LAPIC_LVT_LINT1_REG 0x360
#define LAPIC_LVT_ERROR_REG 0x370
#define LAPIC_LVT_PENDING (0b1 << 12)

#define LAPIC_TIMER_INITIAL_COUNT_REG 0x380
#define LAPIC_TIMER_CURRENT_COUNT_REG 0x390
#define LAPIC_TIMER_DIV_CONFIG_REG 0x3E0

// different for x2apic
// vol 3 11.6.1
#define LAPIC_ICR_REG_LOW 0x300
#define LAPIC_ICR_REG_HIGH 0x310
enum LAPIC_ICR_DEST {
    ICR_DEST_FIELD = 0b00,
    ICR_DEST_SELF = 0b01,
    ICR_DEST_ALL = 0b10,
    ICR_DEST_OTHERS = 0b11
};
#define LAPIC_ICR_DEL_MODE_FIXED 0b000
#define LAPIC_ICR_DEL_MODE_SMI 0b010
#define LAPIC_ICR_DEL_MODE_NMI 0b100
#define LAPIC_ICR_DEL_MODE_INIT 0b101
#define LAPIC_ICR_DEL_MODE_START_UP 0b110
#define LAPIC_ICR_PENDING 0b1

#define LAPIC_TIMER_CALIBRATION_PROBES 30
#define LAPIC_TIMER_CALIBRATION_FREQ 1000

// ioapic
// ============================================================================
void init_ioapic(void);
void ioapic_redirect_irq(uint32_t irq, uint32_t vector, uint32_t lapic_id);
void ioapic_write(struct acpi_ioapic *ioapic, uint32_t reg, uint32_t val);
uint32_t ioapic_read(struct acpi_ioapic *ioapic, uint32_t reg);

// lapic
// ============================================================================
extern struct cpuid_data_common cpuid_data;
extern uintptr_t lapic_address;

void init_lapic(void);
void lapic_write(uint32_t reg, uint32_t val);
uint32_t lapic_read(uint32_t reg);
void lapic_send_eoi_signal(void);
void lapic_send_ipi(uint32_t lapic_id, uint32_t vector, enum LAPIC_ICR_DEST dest);
void lapic_timer_handler(cpu_ctx_t *regs);
void calibrate_lapic_timer(void);
void lapic_timer_periodic(size_t vector, size_t freq);
void lapic_timer_oneshot_us(size_t vector, size_t us);
void lapic_timer_oneshot_ms(size_t vector, size_t ms);
void lapic_timer_halt(void);