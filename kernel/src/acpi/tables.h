#pragma once

#include <stdint.h>

#define MADT_ENTRY_PROCESSOR_LAPIC 0x0
#define MADT_ENTRY_IO_APIC 0x1
#define MADT_ENTRY_INTERRUPT_SOURCE_OVERRIDE 0x2
#define MADT_ENTRY_NON_MASKABLE_ADDRESS_OVERRIDE 0x3
#define MADT_ENTRY_LAPIC_NMI 0x4
#define MADT_ENTRY_LAPIC_ADDRESS_OVERRIDE 0x5
#define MADT_ENTRY_IO_SAPIC 0x6
#define MADT_ENTRY_LSAPIC 0x7
#define MADT_ENTRY_PLATFORM_INTERRUPT_SOURCES 0x8
#define MADT_ENTRY_PROCESSOR_LOCAL_X2APIC 0x9
#define MADT_ENTRY_LOCAL_X2APIC_NMI 0xA
#define MADT_ENTRY_GIC_CPU_INTERFACE 0xB
#define MADT_ENTRY_GIC_DISTRIBUTOR 0xC
#define MADT_ENTRY_GIC_MSI_FRAME 0xD
#define MADT_ENTRY_GIC_REDISTRIBUTOR 0xE
#define MADT_ENTRY_GIC_INTERRUPT_TRANSLATION_SERVICE 0xF
#define MADT_ENTRY_MP_WAKEUP 0x10

// https://uefi.org/htmlspecs/ACPI_Spec_6_4_html
struct rsdp {
    // rsdt size: 20
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_address;  // deprecated >= v2

    // xsdt size: 128 bits; 16 bytes
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__ ((packed));

struct sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[6];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
};

struct rsdt {
  struct sdt_header header;
  // rsdt: uint32_t pointer, xsdt: uin64_t pointer
  uint8_t ptr[];
};

// 5.2.12
struct madt {
    struct sdt_header header;
    uint32_t lapic_address_phys;
    uint32_t flags;
    uint8_t entries[];
};

struct gas {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
};

struct fadt {
    struct sdt_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;

    // field used in ACPI 1.0; no longer in use, for compatibility only
    uint8_t  reserved_0;

    uint8_t preferred_power_management_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command_port;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
    uint8_t pm12_control_length;
    uint8_t pm_timer_length;
    uint8_t gpe0_length;
    uint8_t gpe1_length;
    uint8_t gpe1_base;
    uint8_t c_sate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;

    // reserved in ACPI 1.0; used since ACPI 2.0+
    uint16_t boot_arch_flags;

    uint8_t reserved_1;
    uint32_t flags;

    struct gas reset_reg;

    uint8_t reset_value;
    uint8_t reserved_2[3];

    // 64bit pointers - Available on ACPI 2.0+
    uint64_t x_firmware_control;
    uint64_t x_dsdt;

    struct gas x_pm1a_event_block;
    struct gas x_pm1b_event_block;
    struct gas x_pm1a_control_block;
    struct gas x_pm1b_control_block;
    struct gas x_pm2_control_block;
    struct gas x_pm_timer_block;
    struct gas x_gpe0_block;
    struct gas x_gpe1_block;
};