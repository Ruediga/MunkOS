#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct fat_bpb {
    uint8_t boot_bytes[3];
    uint64_t oem_id;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_clusters;
    uint8_t fats;
    uint16_t number_root_dir_entries;
    uint16_t total_sectors;
    uint8_t media_descriptor_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t nheads;
    uint32_t hidden_sectors;
    uint32_t large_sectors;
};

// fat 32 ebr
struct fat_ebr {
    uint32_t sectors_per_fat;
    uint16_t flags;
    uint8_t fat_version_minor;
    uint8_t fat_version_major;
    uint32_t root_dir_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;   // 0x0 floppy, 0x80 hard disk
    uint8_t nt_flags;
    uint8_t signature;      // 0x28 or 0x29
    uint32_t volume_id;
    uint8_t vol_label_str[11];
    uint64_t sys_id_str;
    uint8_t boot_code[420];
    uint16_t bootable_signature;
};

struct fat_fsinfo {
    uint32_t signature;     // valid: 0x41615252
    uint8_t reserved_0[480];
    uint32_t signature_2;   // valid: 0x61417272
    uint32_t last_free_cluster;     // range check
    uint32_t cluster_start_hint;    // range check
    uint8_t reserved_1[12];
    uint32_t trail_signature;       // valid: 0xAA550000
};