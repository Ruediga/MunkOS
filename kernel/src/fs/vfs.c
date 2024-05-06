#include "vfs.h"

#include "kheap.h"

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

// return a pointer to a newly allocated vnode. Used by vfs_fs
// subnode of owner, flags (), vnode operations, type, specific data pointer for this specific filesystem (needs to be free()able)
static struct vfs_vnode *vfs_vnode_alloc(struct vfs_fs *owner, size_t flags, struct vfs_vnode_ops *ops, enum vfs_vnode_type type, gen_dptr data)
{
    struct vfs_vnode *node = kcalloc(1, sizeof(struct vfs_vnode));
    node->v_flag = flags;
    node->v_count = 0;
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
    // mount some fs to vfs_sroot
    //vfs_mount_root(ext2fs);
}

// unmounts and deallocs all filesystems in linked list
static inline void vfs_free_filesystem_list(struct vfs_fs *root)
{
    while (root->vfs_next && root) {
        root->vfs_op->vfs_unmount(root);
        root = root->vfs_next;
    }
}

static inline int vfs_mount_root(struct vfs_fs *fs)
{
    // if there are any filesystems mounted
    if (vfs_sroot->vfs_next) {
        return 0;
    };

    vfs_sroot = fs;
}
