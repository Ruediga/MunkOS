#pragma once

#include "vfs.h"

#define RAMDISK_DEVICE_SIGNATURE (0xa2b35253eful)

#define RAMDISK_MAX_FILENAME_LENGTH 256

enum ramdisk_file_type {
    RAMD_DIR,
    RAMD_FILE
};

struct ramdisk_file {
    // link together all files in a directory
    struct ramdisk_file *next, *prev;

    enum ramdisk_file_type type;

    // should use struct timeval here
    uint64_t atime,
        mtime,
        ctime,
        btime;

    char name[RAMDISK_MAX_FILENAME_LENGTH];
    union {
        struct {
            struct ramdisk_file *first_subdir;
            int num_entries;
        } directory;

        struct {
            size_t size;
            void *file_contents;
        } file;
    };
};

// simulate a disk so we can easily create, rw to/from and manage temporary files
struct ramdisk {
    uint64_t device_signature;

    // list of ramdisks
    struct ramdisk *next, *prev;

    struct ramdisk_file root_dir;

    struct vfs_vnode_ops vnodeops;
};

// make sure to clean up ramdisk device
struct ramdisk *rd_new_ramdisk(void);

struct vfs_fs *ramfs_new_fs(void);
void ramfs_destroy_fs(struct vfs_fs *ramfs);