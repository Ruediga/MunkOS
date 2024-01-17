#pragma once

#include <stdint.h>
#include <stddef.h>

#define PIT_OSCILLATOR_FREQUENCY 1193182ul

// !!! min. rate = 19Hz
#define PIT_INT_FREQUENCY 1000

void pit_rate_init(uint16_t value);
uint16_t pit_read_current(void);
void pit_reset_to_default(void);
void init_pit(void);