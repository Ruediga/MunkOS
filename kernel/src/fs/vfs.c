#include "vfs.h"
#include "interrupt.h"
#include "kheap.h"
#include "kprintf.h"
#include "ramfs.h"
#include "string.h"
#include "macros.h"

// system wide vfs root point.
struct vfs_fs *vfs_sroot = NULL;

// insert vfs_fs into list when mount()ing
static void vfs_fs_list_insert()
{
    // vfs_sroot.vfs_next is always NULL
}

// remove vfs_fs from list when unmount()ing
static void vfs_fs_list_remove()
{

}

struct vfs_vnode *vfs_vnode_alloc(struct vfs_fs *owner, size_t flags,
    struct vfs_vnode_ops *ops, enum vfs_vnode_type type, gen_dptr data)
{
    struct vfs_vnode *node = kcalloc(1, sizeof(struct vfs_vnode));
    node->v_flag = flags;
    node->v_refc = 0;
    node->v_vfs_mounted_here = NULL;
    node->v_op = ops;
    node->v_vfsp = owner;
    node->v_type = type;
    node->v_data = data;

    return node;
}

static void vfs_vnode_free(struct vfs_vnode **node_ptr)
{
    kfree((*node_ptr)->v_data);
    kfree(*node_ptr);
    *node_ptr = NULL;
}

// return a pointer to a newly allocated vfs_fs.
// operations, flags, block size, fs specific data ptr
static struct vfs_fs *vfs_fs_alloc(struct vfs_fs_ops *ops, size_t flags, size_t bsize, gen_dptr data)
{
    struct vfs_fs *fs = kcalloc(1, sizeof(struct vfs_fs));
    fs->vfs_next = NULL;
    fs->vfs_op = ops;
    fs->vfs_vnodecovered = NULL;
    fs->vfs_flag = flags;
    fs->vfs_bsize = bsize;
    fs->vfs_data = data;

    return fs;
}

static void vfs_fs_free(struct vfs_vnode **node)
{
    kfree((*node)->v_data);
    kfree(*node);
    *node = NULL;
}

void vfs_init()
{
    kprintf("Initializing vfs\n");

    struct ramdisk *rd = rd_new_ramdisk();
    struct vfs_fs *ramfs = ramfs_new_fs();

    if (ramfs->vfs_op->vfs_mount(ramfs, "/", rd))
        kpanic(0, NULL, "thats bad 1\n");

    struct vfs_vnode *sysroot;
    if (ramfs->vfs_op->vfs_root(ramfs, &sysroot))
        kpanic(0, NULL, "thats bad 2\n");

    kprintf("sysroot type: %lu\n", (uint64_t)sysroot->v_type);


    if (!sysroot->v_op->vn_create)
        kpanic(0, NULL, "create not implemented\n");

    struct vfs_vattr attribs = {
        .va_type = VFS_VN_REG,
    };
    struct vfs_vnode *new_file = NULL;

    if (sysroot->v_op->vn_create(sysroot, "file", &attribs, &new_file))
        kpanic(0, NULL, "create failed\n");
    kprintf("created file \"file\" in root directory of ramdisk\n");


    // write a string to the fs
    struct vfs_uio uio = {
        .uio = UIO_WRITE,
        .uio_buf = (uint8_t *)"Hello World",
        .uio_offset = 0,
        .uio_resid = 12
    };
    new_file->v_op->vn_rdwr(new_file, &uio, 0);

    // read it again
    uio.uio = UIO_READ;
    uio.uio_resid = 12;
    uio.uio_buf = kcalloc(1, 12);
    new_file->v_op->vn_rdwr(new_file, &uio, 0);

    kprintf("string written and read: <%s>\n", uio.uio_buf);


    // try to find the file via lookup
    struct vfs_vnode *found;
    if (sysroot->v_op->vn_lookup(sysroot, "file", &found))
        kpanic(0, NULL, "failed to lookup path\n");

    kfree(sysroot);
}

// unmounts and deallocs all filesystems in linked list
static inline void vfs_free_filesystem_list(struct vfs_fs *root)
{
    while (root->vfs_next && root) {
        root->vfs_op->vfs_unmount(root);
        root = root->vfs_next;
    }
}