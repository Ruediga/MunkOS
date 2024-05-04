#pragma once

#include <stdint.h>
#include <stddef.h>

#define SYSTEM_TIMER_FREQUENCY 1000

#define PIT_OSCILLATOR_FREQUENCY 1193182ul
// min. rate = 19Hz
#define PIT_INT_FREQUENCY 1000

#define RTC_BASE_FREQ 32768000ul
#define TIME_CURRENT_YEAR 2023u

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;      // rtc saves year % 100, so we have to extend it ourselves
    uint8_t century;    // DO NOT rely on century, the correct century is encoded into year
} rtc_time_ctx_t;

struct tscp_ctx {
    uint64_t ts;    // timestamp value
    uint32_t sig;   // OS initialized value (Linux: logical cpu id)
};

struct ktimer {
    int unused;
};

extern volatile size_t system_ticks;
extern volatile size_t unix_time;

// pit
void pit_rate_set(size_t freq);
uint16_t pit_read_current(void);
void init_pit(void);

// rtc
void rtc_set_periodic(uint8_t enable);
void rtc_set_rate(uint8_t rate);
uint8_t rtc_rd_int_type(void);
void time_init(void);
rtc_time_ctx_t rd_rtc(void);
size_t rtc_time2unix_stamp(rtc_time_ctx_t rtc_time);

// tsc
int tsc_supported();
int tscp_supported();
int tsc_invariant();
uint64_t rdtsc();
struct tscp_ctx rdtscp();
uint64_t cpu_base_freq();