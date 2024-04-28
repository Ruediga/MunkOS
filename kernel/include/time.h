#pragma once

#include <stdint.h>
#include <stddef.h>

#define PIT_OSCILLATOR_FREQUENCY 1193182ul

// min. rate = 19Hz
#define PIT_INT_FREQUENCY 1000

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} rtc_time_ctx_t;

// remove
extern volatile size_t pit_ticks;

void pit_rate_init(size_t freq);
uint16_t pit_read_current(void);
void init_pit(void);

// rtc
#define TIME_RTC_BASE_FREQ 32768000ul

void common_timer_handler(void);
void wrt_cmos_cfg_space(uint8_t reg, uint8_t value);
void rtc_set_periodic(uint8_t enable);
void rtc_set_rate(uint8_t rate);
uint8_t rtc_rd_int_type(void);
void rtc_init(void);
void time_init(void);