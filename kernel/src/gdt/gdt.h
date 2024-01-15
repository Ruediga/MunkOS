#pragma once
#include <stdint.h>

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
} __attribute__((packed)) segment_descriptor;

// for tss and ldt
typedef struct {
    segment_descriptor descriptor;
    uint32_t base;
    uint32_t reserved;
} __attribute__((packed)) system_segment_descriptor;

// task state segment
typedef struct {
    uint32_t res_0;
    uint64_t rsp[3]; // stack pointers
    uint64_t res_1;
    uint64_t ist[7]; // interrupt stack tables
    uint64_t res_2;
    uint16_t res_3;
    uint16_t io_permission_bitmap;
} __attribute__((packed)) tss;

void init_gdt(void);
void rld_gdt();
void rld_tss(uintptr_t tss_address);