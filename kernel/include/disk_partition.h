#pragma once

#include <stdint.h>
#include <stddef.h>

#include "kprintf.h"
#include "device.h"

struct partition_device {
    struct device dev;
    size_t starting_lba;
};

struct gpt_partition_table_header {
    char signature[8];
    uint32_t rev;
    uint32_t header_size;
    uint32_t checksum_header;
    uint32_t reserved;
    uint64_t header_lba;
    uint64_t header_alt_lba;
    uint64_t first_block;
    uint64_t last_block;
    uint8_t guid[16];
    uint64_t guid_pea_start_lba;
    uint32_t partentries_count;
    uint32_t pea_entry_size;
    uint32_t checksum_pea;
    uint8_t reserved_filler[];
};

struct gpt_partition_table_entry {
    uint64_t part_type_guid_low;
    uint64_t part_type_guid_high;
    uint8_t unique_part_guid[16];
    uint64_t lba_start;
    uint64_t lba_end;
    uint64_t attribs;
    char name[];    // len: header.pea_entry_size - 0x38; 2 bytes per char
};

struct mbr_partition_table_entry {
    uint8_t drive_attribs;
    uint8_t part_start_chs[3];
    uint8_t part_type;
    uint8_t part_last_chs[3];
    uint32_t start_lba;
    uint32_t nsectors;
};

struct mbr_partition_format {
    uint8_t bootstrap_code[440];
    uint32_t opt_diskid_signature;  // 0x5A5A means RO
    uint16_t opt;
    struct mbr_partition_table_entry entries[4];
    uint8_t signature_low;
    uint8_t signature_high;
};

void partition_disk_device(struct device *vdisk);