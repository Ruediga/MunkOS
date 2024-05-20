#include "ramfs.h"
#include "frame_alloc.h"
#include "interrupt.h"
#include "kheap.h"
#include "kprintf.h"
#include "process.h"
#include "time.h"
#include "vfs.h"
#include "string.h"
#include "pathn.h"
#include <string.h>

// this is NOT a ramdisk in the sense of ram device, but a simple implementation
// to create temporary files in ram

// supported file types: VFS_VN_DIR, VFS_VN_REG

// vfs_fs.vfs_data = struct ramdisk *device
// vfs_vnode.v_data = struct ramdisk_file *file

int ramfs_mount(struct vfs_fs *this, gen_dptr fs_specific);
int ramfs_unmount(struct vfs_fs *this);
int ramfs_root(struct vfs_fs *this, struct vfs_vnode **result);
int ramfs_statfs(struct vfs_fs *this, struct vfs_statfs *result);
int ramfs_sync(struct vfs_fs *this);
int ramfs_fid(struct vfs_fs *this, struct vfs_vnode *file, struct vfs_fid **result);
int ramfs_vget(struct vfs_fs *this, struct vfs_vnode **result, struct vfs_fid *id);

int ramfs_vn_open(struct vfs_vnode *this, uint64_t flags);
int ramfs_vn_close(struct vfs_vnode *this, uint64_t flags);
int ramfs_vn_rdwr(struct vfs_vnode *this, struct vfs_uio *uiop, size_t flags);
int ramfs_vn_lookup(struct vfs_vnode *dir, const char *pathn, size_t len, struct vfs_vnode **result);
int ramfs_vn_create(struct vfs_vnode *dir, const char *filen, size_t len,
        struct vfs_vattr *attribs, struct vfs_vnode **result);

struct ramdisk *ramdisks;

// make sure to clean up ramdisk device
struct ramdisk *rd_new_ramdisk(void)
{
    struct ramdisk *new_rd = kcalloc(1, sizeof(struct ramdisk));

    new_rd->device_signature = RAMDISK_DEVICE_SIGNATURE;

    new_rd->root_dir.atime = new_rd->root_dir.btime = new_rd->root_dir.ctime =
        new_rd->root_dir.mtime = get_unixtime();

    new_rd->vnodeops.vn_open = ramfs_vn_open;
    new_rd->vnodeops.vn_close = ramfs_vn_close;
    new_rd->vnodeops.vn_rdwr = ramfs_vn_rdwr;
    new_rd->vnodeops.vn_lookup = ramfs_vn_lookup;
    new_rd->vnodeops.vn_create = ramfs_vn_create;

    return new_rd;
}

struct ramdisk_file *rd_create_file(struct ramdisk_file *dir, enum ramdisk_file_type type, const char *name, size_t len)
{
    if (dir->type != RAMD_DIR)
        return NULL;

    struct ramdisk_file *file = kcalloc(1, sizeof(struct ramdisk_file));
    file->atime = file->btime = file->ctime = file->mtime = get_unixtime();
    file->type = (enum ramdisk_file_type)type;
    pathn_buffer_n(file->name, name, len);
    file->name_len = len;

    return file;
}

// create a new ramfilesystem vfs_fs.
struct vfs_fs *ramfs_new_fs(void)
{
    struct vfs_fs *ramfs = kcalloc(1, sizeof(struct vfs_fs));
    struct vfs_fs_ops *ramfs_ops = kcalloc(1, sizeof(struct vfs_fs_ops));

    ramfs_ops->vfs_mount = ramfs_mount;
    ramfs_ops->vfs_unmount = ramfs_unmount;
    ramfs_ops->vfs_fid = ramfs_fid;
    ramfs_ops->vfs_root = ramfs_root;
    ramfs_ops->vfs_sync = ramfs_sync;
    ramfs_ops->vfs_vget = ramfs_vget;
    ramfs_ops->vfs_statfs = ramfs_statfs;

    ramfs->vfs_op = ramfs_ops;
    ramfs->name = "ramfs";

    return ramfs;
}

// free ramfs and resources associated
void ramfs_destroy_fs(struct vfs_fs *ramfs)
{
    kfree(ramfs->vfs_op);
    kfree(ramfs);
}



// vfs_fs_ops for ramfs
// ====================

// fs_specific for the ramfs has to be a pointer to a struct ramdisk
int ramfs_mount(struct vfs_fs *this, gen_dptr fs_specific)
{
    if (((struct ramdisk *)fs_specific)->device_signature != RAMDISK_DEVICE_SIGNATURE)
        return VFS_FS_OP_STATUS_INVALID_ARGS;

    this->vfs_data = fs_specific;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_unmount(struct vfs_fs *this)
{
    if (((struct ramdisk *)this->vfs_data)->device_signature != RAMDISK_DEVICE_SIGNATURE)
        return VFS_FS_OP_STATUS_INVALID_ARGS;

    this->vfs_data = NULL;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_root(struct vfs_fs *this, struct vfs_vnode **result)
{
    struct ramdisk *rd = this->vfs_data;

    struct vfs_vnode *vnode = vfs_vnode_alloc(this, VFS_VN_FLAG_ROOT, &rd->vnodeops,
        VFS_VN_DIR, &rd->root_dir);

    *result = vnode;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_statfs(struct vfs_fs *this, struct vfs_statfs *result)
{
    (void)this;
    (void)result;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_sync(struct vfs_fs *this)
{
    (void)this;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_fid(struct vfs_fs *this, struct vfs_vnode *file, struct vfs_fid **result)
{
    (void)this;
    (void)file;
    (void)result;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_vget(struct vfs_fs *this, struct vfs_vnode **result, struct vfs_fid *id)
{
    (void)this;
    (void)id;

    *result = NULL;

    return VFS_FS_OP_STATUS_OK;
}



// vfs_vnode_ops for ramfs
// =======================

int ramfs_vn_open(struct vfs_vnode *this, uint64_t flags)
{
    (void)this;
    (void)flags;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_vn_close(struct vfs_vnode *this, uint64_t flags)
{
    (void)this;
    (void)flags;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_vn_rdwr(struct vfs_vnode *this, struct vfs_uio *uiop, size_t flags)
{
    (void)this;
    (void)flags;

    struct ramdisk_file *file = (struct ramdisk_file *)this->v_data;

    if (uiop->uio == UIO_READ) {
        memcpy(uiop->uio_buf, file->file.file_contents, uiop->uio_resid);
    } else if (uiop->uio == UIO_WRITE) {
        // [FIXME] missing free
        file->file.file_contents = kmalloc(uiop->uio_resid);
        memcpy(file->file.file_contents, uiop->uio_buf, uiop->uio_resid);
    } else kpanic(0, NULL, "oof\n");

    uiop->uio_resid = 0;

    return VFS_FS_OP_STATUS_OK;
}

int ramfs_vn_lookup(struct vfs_vnode *dir, const char *pathn, size_t len, struct vfs_vnode **result)
{
    struct ramdisk_file *file = (struct ramdisk_file *)dir->v_data;
    if (file->type != RAMD_DIR)
        return VFS_FS_OP_STATUS_INVALID_ARGS;

    file = file->directory.first_subdir;

    while (file) {
        if (file->name_len == len && !strncmp(pathn, file->name, len)) {
            struct vfs_vnode *vnode = vfs_vnode_alloc(dir->v_vfsp,
                0, dir->v_op, (enum vfs_vnode_type)file->type, file);
            *result = vnode;
            return VFS_FS_OP_STATUS_OK;
        }
        file = file->next;
    }

    return VFS_FS_OP_STATUS_ENOENT;
}

int ramfs_vn_create(struct vfs_vnode *dir, const char *filen, size_t len,
        struct vfs_vattr *attribs, struct vfs_vnode **result)
{
    (void)attribs;

    struct ramdisk_file *rd_dir = (struct ramdisk_file *)dir->v_data;
    enum ramdisk_file_type t;
    if (attribs->va_type == VFS_VN_DIR)
        t = RAMD_DIR;
    else if (attribs->va_type == VFS_VN_REG)
        t = RAMD_FILE;
    else
        return VFS_FS_OP_STATUS_INVALID_ARGS;

    struct ramdisk_file *new = rd_create_file(rd_dir, t, filen, len);

    // link file into dir list
    new->prev = NULL;
    new->next = rd_dir->directory.first_subdir;
    if (rd_dir->directory.first_subdir)
        rd_dir->directory.first_subdir->prev = new;
    rd_dir->directory.first_subdir = new;

    *result = vfs_vnode_alloc(dir->v_vfsp, 0, &((struct ramdisk *)dir->v_vfsp->vfs_data)->vnodeops,
        attribs->va_type, new);

    if (!*result)
        return VFS_FS_OP_STATUS_INVALID_ARGS;

    return VFS_FS_OP_STATUS_OK;
}