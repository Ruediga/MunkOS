#pragma once
#include <stdint.h>

/*
 * Also see here: https://wiki.osdev.org/Global_Descriptor_Table#Segment_Descriptor
 * and here: http://www.osdever.net/bkerndev/Docs/gdt.htm
 * Layout:
 * 16b limit
 * 16b base
 * 8b base
 * 8b access byte (P / DPL 2b / S / E / DC / RW / A)
 * 4b limit
 * 4b flags (G / DB / L / Reserved)
 * 8b base
 * 
 * limit is ignored in long mode!
*/
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access_byte;
    uint8_t limit_high_and_flags;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry;

extern gdt_entry gdt[5];

void initGDT(void);

void loadGDT(void);