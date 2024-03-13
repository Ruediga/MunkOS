#pragma once

#include <stddef.h>
#include <stdint.h>

typedef void *gen_dptr;

struct vfs_fs_ops;
struct vfs_vnode;
struct vfs_vnode_ops;

enum vfs_vnode_type {VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VBAD};

enum vfs_uio_rw_type {READ, WRITE};

typedef struct {
    uint64_t fsid[2];
} vfs_fsid_t;

// [TODO]
struct vfs_fid {
    uint64_t data;
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
struct vfs_fs {
    struct vfs_fs *vfs_next;            // list of vfs_fs mounted insidehis vfs_fs
    struct vfs_fs_ops *vfs_op;
    struct vfs_vnode *vfs_vnodecovered; // covered vnode
    size_t vfs_flag;
    size_t vfs_bsize;
    gen_dptr vfs_data;                  // private fs specific data
};

// each filesystem needs to implement these
struct vfs_fs_ops {
    int (*vfs_mount)(struct vfs_fs *, const char *, gen_dptr);  // this, path, fs_specific
    int (*vfs_unmount)(struct vfs_fs *);                        // this
    int (*vfs_root)(struct vfs_fs *, struct vfs_vnode **);      // this, result
    int (*vfs_statfs)(struct vfs_fs *, struct vfs_statfs *);    // this, result
    int (*vfs_sync)(struct vfs_fs *);                           // this; write out info
    int (*vfs_fid)(struct vfs_fs *, struct vfs_vnode *, struct vfs_fid **);     // this, node, result
    int (*vfs_vget)(struct vfs_fs *, struct vfs_vnode **, struct vfs_fid *);    // this, result, id
};

struct vfs_vnode {
    size_t v_flag;
    size_t v_count;         // refs to this node
    //u_short v_shlockc; /* # of shared locks */
    //u_short v_exlockc; /* # of exclusive locks */
    struct vfs_fs *v_vfs_mounted_here;  // covering vfs_fs
    struct vfs_vnode_ops *v_op;
    //union {
    //    struct socket *v_Socket; /* unix ipc */
    //    struct stdata *v_Stream; /* stream */
    //};
    struct vfs_fs *v_vfsp;  // we are part of vfs_fs
    enum vfs_vnode_type v_type;
    gen_dptr v_data;
};

struct vfs_vnode_ops {
    int (*vn_open)(struct vfs_vnode **, uint64_t, uint8_t *);   // result, flags, credentials (struct)
    int (*vn_close)(struct vfs_vnode *, uint64_t, uint8_t *);    // this, flags, cred
    int (*vn_rdwr)(struct vfs_vnode *, struct vfs_uio *, uint8_t, size_t, uint8_t *);    // this, uiop, rw, flags, cread
    int (*vn_ioctl)();
    int (*vn_select)();
    int (*vn_getattr)();
    int (*vn_setattr)();
    int (*vn_access)();
    int (*vn_lookup)(struct vfs_vnode *, const char *, struct vfs_vnode **, uint8_t *);  // this, path, result, cred
    int (*vn_create)(struct vfs_vnode *, const char *, struct vfs_vattr *, uint8_t, uint8_t, struct vfs_vnode **, uint8_t *); // dir, filename, attribs, exclusive, mode, result, cred
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

// methods for the kernel vfs subsystem
int vfs_open();
struct vfs_vnode *vfs_create(const char *path);