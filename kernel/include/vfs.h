#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rbtree.h"

#define VFS_FS_OP_STATUS_OK                     0
#define VFS_FS_OP_STATUS_INVALID_ARGS           1
#define VFS_FS_OP_STATUS_MOUNTPOINT_INVALID     2
#define VFS_FS_OP_STATUS_ENOENT                 3

#define VFS_VN_FLAG_ROOT                        (1 << 0)

typedef void *gen_dptr;

enum vfs_vnode_type;
enum vfs_uio_rw_type;
struct vfs_fid;
struct vfs_uio;
struct vfs_statfs;
struct vfs_vattr;
struct vfs_fs;
struct vfs_fs_ops;
struct vfs_vnode;
struct vfs_vnode_ops;
struct pnc_entry;
struct file_handle;

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

// this is not a file descriptor for userspace, but rather a file descriptor
// for the kernel. the vnode field inside the cache_entry may not be invalidated during
// the lifespan of cache_entry.
typedef struct file_handle {
    struct pnc_entry *cache_entry;      // corresponding pnc entry for this file
    struct vfs_fs *fs;                  // file system this file belongs to
} file_handle_t;

// a filesystem that's been mounted to our vfs
struct vfs_fs {
    struct vfs_fs *vfs_next;                // list of vfs_fs mounted inside this vfs_fs
    struct vfs_fs_ops *vfs_op;

    size_t vfs_flag;
    size_t vfs_bsize;

    struct pnc_entry *vfs_root_pnc;         // the root pnc entry for this fs
    struct file_handle vfs_covered;         // we have been mounted on this file handle

    char *name;                             // generic name
    gen_dptr vfs_data;                      // private fs specific data
};

// each filesystem needs to implement these
struct vfs_fs_ops {
    int (*vfs_mount)(struct vfs_fs *this, gen_dptr fs_specific);
    int (*vfs_unmount)(struct vfs_fs *this);
    // return the root vnode for this file system
    int (*vfs_root)(struct vfs_fs *this, struct vfs_vnode **result);
    int (*vfs_statfs)(struct vfs_fs *this, struct vfs_statfs *result);
    // schedule writing out all cached data
    int (*vfs_sync)(struct vfs_fs *this);
    int (*vfs_fid)(struct vfs_fs *this, struct vfs_vnode *file, struct vfs_fid **result);
    int (*vfs_vget)(struct vfs_fs *this, struct vfs_vnode **result, struct vfs_fid *id);
};

// parent can be obtained over the pnc, since there can't ever be a vnode reference
// if the vnode isn't cached, and every cache has to have a valid parent.
struct vfs_vnode {
    enum vfs_vnode_type v_type;
    struct vfs_vnode_ops *v_op;
    struct vfs_fs *v_vfsp;              // we are part of vfs_fs
    struct vfs_fs *v_vfs_mounted_here;  // covering vfs_fs

    // VFS_VN_FLAG_ROOT
    size_t v_flag;
    size_t v_refc;
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
    int (*vn_lookup)(struct vfs_vnode *dir, const char *pathn, size_t len, struct vfs_vnode **result);
    // create a new file, filename, in directory, return its vnode
    int (*vn_create)(struct vfs_vnode *dir, const char *filen, size_t len,
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

// pathname cache entry for vnodes.
struct pnc_entry {
    size_t key;                     // don't move to comply with struct rbtree_data layout

    rb_tree_node_t *entries_root;   // files in this directory
    struct pnc_entry *parent;       // parent cache entry

    struct pnc_entry *next, *prev;  // bucket of the same hashes

    struct vfs_vnode *vnode;

    size_t name_len;
    char *path_section;
};

extern struct vfs_fs *root_fs;
extern const file_handle_t null_handle;
#define NULL_HANDLE (null_handle)

static inline size_t pathn_hash(const char *str, size_t len) {
    size_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = (hash * 33) + str[i];
    }
    return hash;
}

struct pnc_entry *new_pnc_entry(struct vfs_vnode *vnode, struct pnc_entry *parent, const char *pathn, size_t len, size_t hash);
// add a given path section inside dir to the path name cache. pathn gets copied.
struct pnc_entry *pnc_add_section(struct vfs_vnode *vnode, struct pnc_entry *dir, const char *section, size_t len);
// evict a given path section from the cache. recursively invalidates all subentries.
void pnc_evict_section(struct pnc_entry *entry);
// find a path section and return it's pnc_entry object.
struct pnc_entry *pnc_lookup_section(struct pnc_entry *dir, const char *section, size_t len);


void pnc_debugprint(struct pnc_entry *rootdir, size_t depth);

// kernel interface

//...

// how to we do this properly... how can we keep nodes for the same files?
struct vfs_vnode *vfs_vnode_alloc(struct vfs_fs *owner, size_t flags,
    struct vfs_vnode_ops *ops, enum vfs_vnode_type type, gen_dptr data);


void vfs_init();



const char *split_path(const char *path, size_t *length);