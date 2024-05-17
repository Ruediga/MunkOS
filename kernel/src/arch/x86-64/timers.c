#include "macros.h"
#include "io.h"
#include "smp.h"
#include "apic.h"
#include "time.h"
#include "cpu.h"
#include "interrupt.h"
#include "kprintf.h"
#include "_acpi.h"
#include "memory.h"

#define CMOS_SELECT ((uint8_t)0x70)
#define CMOS_DATA ((uint8_t)0x71)

#define BCD2BIN(val) (val = (val & 0x0F) + ((val >> 4) * 10))
#define GDN_EPOCH 719162

// ============================================================================
// pit
// ============================================================================

#pragma region pit

// channel 0 mode 2, rate = Hz
void pit_rate_set(size_t freq)
{
    uint16_t reload_value = (uint16_t)DIV_ROUNDUP(PIT_OSCILLATOR_FREQUENCY, freq);
    outb(0x43, 0b00110100); // command register
    outb(0x40, (uint8_t)reload_value); // low first
    outb(0x40, (uint8_t)(reload_value >> 8)); // high after
}

uint16_t pit_read_current(void)
{
    // counter latch command on channel 0 (bits [7:6])
    outb(0x43, 0b00000000);
    uint16_t count = (uint16_t)inb(0x40);
    return count | ((uint16_t)inb(0x40) << 8);
}

volatile size_t pit_ticks = 0;
static void pit_handler(cpu_ctx_t *regs)
{
    (void)regs;
    pit_ticks++;
    lapic_send_eoi_signal();
}

void init_pit(void)
{
    pit_rate_set(PIT_INT_FREQUENCY);

    interrupts_register_vector(INT_VEC_PIT, (uintptr_t)pit_handler);
    ioapic_redirect_irq(0, INT_VEC_PIT, smp_request.response->bsp_lapic_id);
}

#pragma endregion pit

// ============================================================================
// rtc
// ============================================================================

#pragma region rtc

static inline void wrt_cmos_cfg_space(uint8_t reg, uint8_t value)
{
    switch (reg) {
        case 0x0A: case 0x0B: case 0x0C: case 0x0D: break;
        default: kpanic(0, NULL, "invalid cmos cfg space register selector");
    }

    int_status_t state = preempt_fetch_disable();
    outb(CMOS_SELECT, 0x80 | reg);     // select / disable nmi
    arch_spin_hint();
    outb(CMOS_DATA, value);          // write
    nmi_enable();
    preempt_restore(state);
}
 
// 1024hz
inline void rtc_set_periodic(uint8_t enable)
{
    int_status_t state = preempt_fetch_disable();
    outb(CMOS_SELECT, 0x8B);            // select / disable nmi
    uint8_t prev = inb(CMOS_DATA);      // save
    outb(CMOS_SELECT, 0x8B);            // restore (read resets selector to reg D)
    if (enable)
        outb(CMOS_DATA, 0x40 | prev);   // set reg B bit 6
    else
        outb(CMOS_DATA, ~0x40 & prev);  // unset reg B bit 6
    //nmi_enable();
    preempt_restore(state);
}

// 0 (off) || 2 (8khz) - 15(2hz) (default 6 = 1024hz): freq = 32768 >> (rate - 1)
inline void rtc_set_rate(uint8_t rate)
{
    if ((rate < 3 && rate) || rate > 15)
        kpanic(0, NULL, "invalid rtc rate");

    int_status_t state = preempt_fetch_disable();
    outb(CMOS_SELECT, 0x8A);		    // select A, disable nmi
    uint8_t prev = inb(CMOS_DATA);	    // save
    outb(CMOS_SELECT, 0x8A);		    // restore (read resets selector to reg D)
    outb(CMOS_DATA, (prev & 0xF0) | rate);   // rate low 4 bits
    nmi_enable();
    preempt_restore(state);
}

// types: alarm, update ended, periodic
// flush this or interrupt won't happen again
inline uint8_t rtc_rd_int_type(void)
{
    outb(CMOS_SELECT, 0x0C);   // select reg C
    return inb(CMOS_DATA);
}

static inline uint8_t rtc_get_update_in_progress_flag(void)
{
    outb(CMOS_SELECT, 0x0A);
    return inb(CMOS_DATA) & 0x80;
}

static inline uint8_t get_rtc_register(uint8_t reg)
{
    outb(CMOS_SELECT, reg);
    return inb(CMOS_DATA);
}

rtc_time_ctx_t rd_rtc(void)
{
    rtc_time_ctx_t last = {};
    rtc_time_ctx_t curr = {};
    uint8_t binary_reg;

    while (rtc_get_update_in_progress_flag()) ;

    curr.second = get_rtc_register(0x00);
    curr.minute = get_rtc_register(0x02);
    curr.hour = get_rtc_register(0x04);
    curr.day = get_rtc_register(0x07);
    curr.month = get_rtc_register(0x08);
    curr.year = (uint16_t)get_rtc_register(0x09);
    if (fadt_ptr->century != 0) {
        curr.century = get_rtc_register(fadt_ptr->century);
    }

    // read registers until you get the same value twice in a row
    do {
        last.second = curr.second;
        last.minute = curr.minute;
        last.hour = curr.hour;
        last.day = curr.day;
        last.month = curr.month;
        last.year = curr.year;
        last.century = curr.century;

        while (rtc_get_update_in_progress_flag()) ;

        curr.second = get_rtc_register(0x00);
        curr.minute = get_rtc_register(0x02);
        curr.hour = get_rtc_register(0x04);
        curr.day = get_rtc_register(0x07);
        curr.month = get_rtc_register(0x08);
        curr.year = (uint16_t)get_rtc_register(0x09);
        if (fadt_ptr->century != 0) {
            curr.century = get_rtc_register(fadt_ptr->century);
        }
    } while (memcmp(&curr, &last, sizeof(rtc_time_ctx_t)));

    binary_reg = get_rtc_register(0x0B);

    if (!(binary_reg & 0x04)) {
        // bcd to binary
        BCD2BIN(curr.second);
        BCD2BIN(curr.minute);
        curr.hour = ((curr.hour & 0x0F) + (((curr.hour & 0x70) >> 4) * 10) ) | (curr.hour & 0x80);
        BCD2BIN(curr.day);
        BCD2BIN(curr.month);
        BCD2BIN(curr.year);
        if(fadt_ptr->century != 0) {
            BCD2BIN(curr.century);
        }
    }

    if (!(binary_reg & 0x02) && (curr.hour & 0x80)) {
        curr.hour = ((curr.hour & 0x7F) + 12) % 24;
    }

    if (fadt_ptr->century != 0) {
        curr.year += curr.century * 100;
    } else {
        curr.year = (TIME_CURRENT_YEAR / 100) * 100;
        if (curr.year < TIME_CURRENT_YEAR)
            curr.year += 100;
    }

    return curr;
}

// returns the gregorian day number of january 1st of a given year from a given gregorian calendar year
static inline uint64_t year_to_gdn(uint64_t year, int *is_leap) {
    year--;
    uint64_t cycle = year / 400;
    uint64_t year_in_cycle = year % 400;
    uint64_t century = year_in_cycle / 100;
    uint64_t year_in_century = year_in_cycle % 100;
    uint64_t group = year_in_century / 4;
    uint64_t year_in_group = year_in_century % 4;
    if (is_leap)
    *is_leap = year_in_group == 3 && (year_in_century != 99 || year_in_cycle == 399);
    // 146097 days per cycle + 36524 days per century + 1461 per 4yr cycle + 365 days non leap year
    return 146097 * cycle + 36524 * century + 1461 * group + 365 * year_in_group;
}

static inline int rtc_time_to_gdn(const rtc_time_ctx_t *ctx) {
    static const uint16_t month_to_gdn[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int is_leap;

    int gdn = year_to_gdn(ctx->year + 1900, &is_leap);
    gdn += month_to_gdn[ctx->month] + (is_leap && ctx->month > 1);
    gdn += ctx->day - 1;

    return gdn;
}

// 1970 = 4 cycles, 3 centuries, 17 groups and 1 year after january 1st
size_t rtc_time2unix_stamp(rtc_time_ctx_t rtc_time)
{
    rtc_time.year -= 1900;
    rtc_time.month--;
    int gdn = rtc_time_to_gdn(&rtc_time);
    return (gdn - GDN_EPOCH) * 86400ul + rtc_time.hour * 3600 + rtc_time.minute * 60 + rtc_time.second;
}

#pragma endregion rtc

// ============================================================================
// tsc
// ============================================================================

#pragma region tsc

inline int tsc_supported() {
    struct cpuid_ctx ctx = {.leaf = 0x1};
    cpuid(&ctx);
    return (ctx.edx & 0b1000) != 0;
}

inline int tscp_supported() {
    struct cpuid_ctx ctx = {.leaf = 0x80000001};
    cpuid(&ctx);
    return (ctx.edx & (1 << 27)) != 0;
}

inline int tsc_invariant() {
    struct cpuid_ctx ctx = {.leaf = 0x80000007};
    cpuid(&ctx);
    return (ctx.edx & 0x100) != 0;
}

inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi) ::);
    return ((uint64_t)hi << 32ul) | lo;
}

inline struct tscp_ctx rdtscp() {
    uint32_t hi;
    struct tscp_ctx ctx;
    __asm__ volatile("rdtscp" : "=a" (ctx.ts), "=d" (hi), "=c" (ctx.sig) ::);
    ctx.ts |= ((uint64_t)hi << 32ul);
    return ctx;
}

inline uint64_t cpu_base_freq() {
    struct cpuid_ctx ctx = {.leaf = 0x0};
    cpuid(&ctx);
    if (ctx.eax < 0x16) {
        // not supported
        return 0;
    }
    ctx.leaf = 0x16;
    cpuid(&ctx);
    return (uint64_t)ctx.eax * 1000000;
}

#pragma endregion tsc

// ============================================================================
// hpet
// ============================================================================

#pragma region hpet

// [TODO]

struct hpet_entry {
	uint32_t general_capabilites_and_id;
	struct acpi_gas address;
	uint8_t hpet_number;
	uint16_t min_tick;
	uint8_t page_prot;
} comp_packed;

//static struct hpet_entry *hpet_ent;

// parse acpi tables, save ptrs
int hpet_init(void)
{
    /*
    uacpi_table *hpet_table;
    uacpi_status state = uacpi_table_find_by_signature("HPET", &hpet_table);
    if (state != UACPI_STATUS_OK) {
        kprintf("[WARNING]: acpi table \'HPET\' does not exist\n");
        return 1;
    }

    if (hpet_table->hdr->length < sizeof(struct acpi_sdt_hdr) + sizeof(struct hpet_entry)) {
        kprintf("[WARNING]: acpi HPET table has no entries\n");
        return 1;
    }

    hpet_ent = (struct hpet_entry *)(hpet_table->virt_addr + sizeof(struct hpet_entry));
    kprintf("setting up HPET...\n");

    mmu_map_single_page_4k(&kernel_pmc, hpet_ent->address.address + hhdm->offset,
        hpet_ent->address.address, PM_COMMON_WRITE | PM_COMMON_PRESENT | PM_COMMON_PCD);
    */

    return 1;
}

#pragma endregion hpet