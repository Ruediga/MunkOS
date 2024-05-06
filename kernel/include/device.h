#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "vfs.h"
#include "cpu.h"
#include "locking.h"

// should be equal to max inode name length
#define DEVICE_NAME_LENGTH 256

#define DEVICE_FLAGS_RO 1

enum dev_type { DEV_BLOCK, DEV_CHAR };

// generic device, pointer should be kept in a vfs_fs.gen_dptr.
// device specific requirements can be added like this:
// struct xydevice {
//     struct device;
//     int x;
//     int y;
// };
// now cast this as needed
// this allow for a seamless integration of devices into vfs_fs specific operations.
struct device {
    enum dev_type type;     // BLK or CHR
    size_t flags;           // nothing yet
    char name[DEVICE_NAME_LENGTH];
    k_spinlock_t lock;      // may be locked at any time
    size_t bsize;           // block size
    size_t bcount;          // blocks in partition
    // each device driver has to implement these and then register the device in the devtmpfs.
    // filesystems use these methods for direct device access by taking the gen_dptr from the devices vnode.
    // for now rw only [TODO] ioctl(), ...
    // read/write count bytes starting at off into buf
    int (*read)(struct device *, void *buf, size_t off, size_t count);
    int (*write)(struct device *, void *buf, size_t off, size_t count);
    gen_dptr dev_specific;
};

void device_init(struct device *dev, size_t bsize, size_t bcount, size_t flags, enum dev_type type, gen_dptr dev_specific, const char *fmt, ...);