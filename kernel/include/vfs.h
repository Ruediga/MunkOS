#pragma once

#include <stddef.h>
#include <stdint.h>

#define VFS_FS_OP_STATUS_OK 0
#define VFS_FS_OP_STATUS_INVALID_ARGS 1

typedef void *gen_dptr;

struct vfs_fs_ops;
struct vfs_vnode;
struct vfs_vnode_ops;

enum vfs_vnode_type {
    VFS_VN_NON,
    VFS_VN_REG,
    VFS_VN_DIR,
    VFS_VN_BLK,
    VFS_VN_CHR,
    VFS_VN_LNK,
    VFS_VN_SCK,
    VFS_VN_BAD
};

enum vfs_uio_rw_type {
    UIO_READ,
    UIO_WRITE
};

typedef struct {
    uint64_t fsid[2];
} vfs_fsid_t;

// [TODO]
struct vfs_fid {
    uint64_t data_len;
    uint8_t data[1];
};

struct vfs_uio {
    uint8_t *uio_buf;   // buffer
    size_t uio_resid;   // remaining bytes
    size_t uio_offset;  // curr offset into buffer
    enum vfs_uio_rw_type uio;   // rw
};

struct vfs_statfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    vfs_fsid_t f_fsid;
    uint64_t f_spare[7];
};

struct vfs_vattr {
    enum vfs_vnode_type va_type;
    uint16_t va_mode;
    int16_t va_uid;
    int16_t va_gid;
    size_t va_fsid;
    size_t va_nodeid;
    int16_t va_nlink;
    size_t va_size;
    //struct timeval va_atime;
    //struct timeval va_mtime;
    //struct timeval va_ctime;
    size_t va_rdev;
    size_t va_blocks;
};

// a filesystem that's been mounted to our vfs
// other filesystems can just take this 
struct vfs_fs {
    struct vfs_fs *vfs_next;            // list of vfs_fs mounted inside this vfs_fs
    struct vfs_fs_ops *vfs_op;
    struct vfs_vnode *vfs_vnodecovered; // covered vnode
    size_t vfs_flag;
    size_t vfs_bsize;
    gen_dptr vfs_data;                  // private fs specific data
};

// each filesystem needs to implement these
struct vfs_fs_ops {
    // mount this to path pathn
    int (*vfs_mount)(struct vfs_fs *this, const char *pathn, gen_dptr fs_specific);
    // unmount this
    int (*vfs_unmount)(struct vfs_fs *this);
    // return the root vnode for this file system
    int (*vfs_root)(struct vfs_fs *this, struct vfs_vnode **result);
    // return file system information
    int (*vfs_statfs)(struct vfs_fs *this, struct vfs_statfs *result);
    // schedule writing out all cached data
    int (*vfs_sync)(struct vfs_fs *this);
    // return file identifier for vnode at file
    int (*vfs_fid)(struct vfs_fs *this, struct vfs_vnode *file, struct vfs_fid **result);
    // return the corresponding vnode for a file identifier
    int (*vfs_vget)(struct vfs_fs *this, struct vfs_vnode **result, struct vfs_fid *id);
};

struct vfs_vnode {
    size_t v_flag;
    size_t v_refc;                      // ref count
    struct vfs_fs *v_vfs_mounted_here;  // covering vfs_fs
    struct vfs_vnode_ops *v_op;
    struct vfs_fs *v_vfsp;              // we are part of vfs_fs
    enum vfs_vnode_type v_type;
    gen_dptr v_data;
};

struct vfs_vnode_ops {
    // perform any open protocol on vnode (e.g. a device)
    int (*vn_open)(struct vfs_vnode *this, uint64_t flags);
    // if device: called when refcount reaches
    // if other: called on last close of a file descriptor to this vnode
    int (*vn_close)(struct vfs_vnode *this, uint64_t flags);
    // read/write from this vnode, buffer and length are stored in uiop
    // rw = 1: write, rw = 0: read
    // flags: [TODO] read synchronously, lock the file...
    int (*vn_rdwr)(struct vfs_vnode *this, struct vfs_uio *uiop, size_t flags);
    int (*vn_ioctl)();
    int (*vn_select)();
    int (*vn_getattr)();
    int (*vn_setattr)();
    int (*vn_access)();
    // look up pathname in directory, return corresponding vnode
    int (*vn_lookup)(struct vfs_vnode *dir, const char *pathn, struct vfs_vnode **result);
    // create a new file, filename, in directory, return its vnode
    int (*vn_create)(struct vfs_vnode *dir, const char *filen,
        struct vfs_vattr *attribs, struct vfs_vnode **result);
    int (*vn_remove)();
    int (*vn_link)();
    int (*vn_rename)();
    int (*vn_mkdir)();
    int (*vn_rmdir)();
    int (*vn_readdir)();
    int (*vn_symlink)();
    int (*vn_readlink)();
    int (*vn_fsync)();
    int (*vn_inactive)();
    int (*vn_bmap)();
    int (*vn_strategy)();
    int (*vn_bread)();
    int (*vn_brelse)();
};

// kernel interface

//...

// how to we do this properly... how can we keep nodes for the same files?
struct vfs_vnode *vfs_vnode_alloc(struct vfs_fs *owner, size_t flags,
    struct vfs_vnode_ops *ops, enum vfs_vnode_type type, gen_dptr data);


void vfs_init();