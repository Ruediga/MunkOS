#pragma once

#include <stdint.h>

#include "compiler.h"

struct task_state_segment;

/*
 * https://wiki.osdev.org/Global_Descriptor_Table#Segment_Descriptor
 * Layout:
 * 16b limit
 * 16b base
 * 8b base
 * 8b access byte (P / DPL 2b / S / E / DC / RW / A)
 * 4b limit
 * 4b flags (G / DB / L / Reserved)
 * 8b base
 * 
 * limit is ignored in long mode
*/
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access_byte;
    uint8_t limit_high_and_flags;
    uint8_t base_high;
} comp_packed segment_descriptor;

// for tss and ldt
typedef struct {
    segment_descriptor descriptor;
    uint32_t base;
    uint32_t reserved;
} comp_packed system_segment_descriptor;

void init_gdt(void);
void rld_gdt();
void rld_tss(struct task_state_segment *tss_address);