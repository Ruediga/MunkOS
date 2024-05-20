#include "vfs.h"
#include "compiler.h"
#include "interrupt.h"
#include "kheap.h"
#include "kprintf.h"
#include "pathn.h"
#include "ramfs.h"
#include "string.h"
#include "macros.h"
#include <threads.h>

// system wide vfs root mount point.
struct vfs_fs *root_fs = NULL;

const file_handle_t null_handle = {
    .cache_entry = NULL,
    .fs = NULL
};

// insert vfs_fs into list when mount()ing
static void vfs_fs_list_insert()
{
    // root_fs.vfs_next is always NULL
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

// unmounts and deallocs all filesystems in linked list
static inline void vfs_free_filesystem_list(struct vfs_fs *root)
{
    while (root->vfs_next && root) {
        root->vfs_op->vfs_unmount(root);
        root = root->vfs_next;
    }
}

// if path doesn't start with root, take start as reference for current dir.
int vfs_lookup(file_handle_t start, const char *path, file_handle_t *out)
{
    file_handle_t current;
    if (*path == '/') {
        // root
        current.cache_entry = root_fs->vfs_root_pnc;
        current.fs = root_fs;
    } else {
        // we start searching from an existing cache entry
        kassert(start.cache_entry && start.fs);
        current = start;
    }

    struct vfs_fs *fs;
    while ((fs = current.cache_entry->vnode->v_vfs_mounted_here)) {

        current.cache_entry = fs->vfs_root_pnc;
        current.fs = fs;
    }

    size_t len = 0;
    const char *section = path;
    file_handle_t last;
    while ((section = split_path(section + len, &len))) {
        // test for mount points
        while ((fs = current.cache_entry->vnode->v_vfs_mounted_here)) {

            current.cache_entry = fs->vfs_root_pnc;
            current.fs = fs;
        }

        last = current;

        // attempt to look up a cached entry
        current.cache_entry = pnc_lookup_section(last.cache_entry, section, len);

        if (current.cache_entry) {
            // path cached

            current.fs = current.cache_entry->vnode->v_vfsp;
            continue;
        }

        // path uncached:
        // attempt to ask file system for new vnode
        // on fail, return
        // on success, cache new vnode
        struct vfs_vnode *vn;
        int state = last.cache_entry->vnode->v_op->vn_lookup(last.cache_entry->vnode, section, len, &vn);
        if (state) {
            // failed to lookup vnode
            out->cache_entry = NULL;
            out->fs = NULL;

            kprintf("  - vfs: lookup -> failed to find ");
            pathn_bt_print(last);
            kprintf("%.*s\n", len, section);

            return state;
        }

        // got a vnode. immediately cache it so it doesn't get created all over again
        current.cache_entry = pnc_add_section(vn, last.cache_entry, section, len);
        current.fs = vn->v_vfsp;
    }

    // we already got the right entry, but for correctness, check if it's
    // actually what has been asked for
    if (path[strlen(path) - 1] == '/') {
        // we expect a directory
        if (current.cache_entry->vnode->v_type != VFS_VN_DIR)
            return VFS_FS_OP_STATUS_ENOENT;
    }

    *out = current;

    kprintf("  - vfs: lookup -> found ");
    pathn_bt_print(*out);
    kprintf("\n");

    return VFS_FS_OP_STATUS_OK;
}

// mount the systems root file system. mounting a file system on the root directory
// of the root_fs IS NOT the same as mounting a root file system
int vfs_mount_root(struct vfs_fs *fs, void *fs_specific)
{
    if (root_fs) {
        kpanic(0, NULL, "root file system is already mounted");
        unreachable();
    }

    int state = fs->vfs_op->vfs_mount(fs, fs_specific);
    if (state)
        return state;

    root_fs = fs;

    // attempt to mount the new file systems root vnode immediately
    struct vfs_vnode *vn;
    fs->vfs_op->vfs_root(fs, &vn);
    fs->vfs_root_pnc = new_pnc_entry(vn, NULL,
        "/", 1, pathn_hash("/", 1));

    kprintf_verbose("  - vfs: mounted root filesystem <%s>\n", fs->name);

    return VFS_FS_OP_STATUS_OK;
}

// attempts to mount a file system at the given mountpoint
int vfs_mount(struct vfs_fs *fs, file_handle_t mountpoint, void *fs_specific)
{
    if (!root_fs || !root_fs->vfs_root_pnc)
        return VFS_FS_OP_STATUS_MOUNTPOINT_INVALID;

    // do this tmrw!!!!!!!!!!!!!!!!!!!!!!!!!!
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // lookup:
    // if (curr_hnd.vnode.vfs_mounted_here):
    //     get this filesystems root pnc_entry
    //     traverse from there
    // .. : just go to vfs_fs.vnode_covered
    //     If a root vnode is encountered (VROOT flag
    //     in v_flag set) when following "..", lookuppn follows the vfs_vnodecovered pointer
    //     in the vnode’s associated vfs to obtain the covered vnode.
    //
    //     think about how to manage that with caching, and how to handle
    //     sysroot fs

    int state = fs->vfs_op->vfs_mount(fs, fs_specific);
    if (state)
        return state;

    struct vfs_vnode *mountnode = mountpoint.cache_entry->vnode;
    mountnode->v_vfs_mounted_here = fs;

    fs->vfs_covered.cache_entry = mountpoint.cache_entry;
    fs->vfs_covered.fs = fs;

    // attempt to mount the new file systems root vnode immediately
    struct vfs_vnode *vn;
    fs->vfs_op->vfs_root(fs, &vn);
    fs->vfs_root_pnc = new_pnc_entry(vn, NULL, mountpoint.cache_entry->path_section,
        mountpoint.cache_entry->name_len, pathn_hash("/", 1));

    kprintf("  - vfs: mounted filesystem <%s> at ", fs->name);
    pathn_bt_print(mountpoint);
    kprintf("\n");

    return VFS_FS_OP_STATUS_OK;
}

int vfs_create(file_handle_t dir, const char *path, size_t len, struct vfs_vattr *attribs, file_handle_t *out)
{
    struct vfs_vnode *vn = dir.cache_entry->vnode;
    if (vn->v_type != VFS_VN_DIR)
        return VFS_FS_OP_STATUS_INVALID_ARGS;

    struct vfs_vnode *out_vn;
    int state = vn->v_op->vn_create(vn, path, len, attribs, &out_vn);
    if (state)
        return state;

    out->cache_entry = pnc_add_section(out_vn, dir.cache_entry, path, len);
    out->fs = out->cache_entry->vnode->v_vfsp;

    kprintf("  - vfs: created file ");
    pathn_bt_print(*out);
    kprintf("\n");

    return VFS_FS_OP_STATUS_OK;
}

int vfs_rdwr(file_handle_t file, struct vfs_uio *uio, size_t flags)
{
    struct vfs_vnode *vn = file.cache_entry->vnode;
    if (vn->v_type == VFS_VN_DIR)
        return VFS_FS_OP_STATUS_INVALID_ARGS;

    size_t bytes2read = uio->uio_resid;

    int state = vn->v_op->vn_rdwr(vn, uio, flags);
    kprintf("  - vfs: %s file ", uio->uio == UIO_READ ? "read from" : "wrote to");
    pathn_bt_print(file);
    kprintf(" (%lu out of %lu bytes)\n", bytes2read - uio->uio_resid, bytes2read);
    return state;
}

void vfs_init()
{
    kprintf("Initializing vfs\n");

    struct ramdisk *rd_root = rd_new_ramdisk();
    struct vfs_fs *ramfs_root = ramfs_new_fs();

    vfs_mount_root(ramfs_root, rd_root);

    file_handle_t dest;
    int state = vfs_lookup(NULL_HANDLE, "/", &dest);
    kassert(state == 0);

    struct vfs_vattr attribs = {
        .va_type = VFS_VN_REG
    };
    file_handle_t out;
    state = vfs_create(dest, "testfile", 8, &attribs, &out);
    kassert(state == 0);

    struct vfs_uio uio = {
        .uio = UIO_WRITE,
        .uio_buf = (uint8_t *)"Hello World",
        .uio_offset = 0,
        .uio_resid = 12
    };
    state = vfs_rdwr(out, &uio, 0);
    kassert(state == 0);

    // read it again
    uio.uio = UIO_READ;
    uio.uio_resid = 12;
    uio.uio_buf = kcalloc(1, 12);
    state = vfs_rdwr(out, &uio, 0);
    kassert(state == 0);

    kfree(uio.uio_buf);



    attribs.va_type = VFS_VN_DIR;
    state = vfs_create(dest, "testdir", 7, &attribs, &out);
    kassert(state == 0);

    state = vfs_lookup(NULL_HANDLE, "/testdir", &dest);
    kassert(state == 0);

    attribs.va_type = VFS_VN_REG;
    state = vfs_create(dest, "fileinsidedir", 13, &attribs, &out);
    kassert(state == 0);

    state = vfs_lookup(NULL_HANDLE, "/testdir/fileinsidedir", &dest);
    kassert(state == 0);

    uio.uio = UIO_WRITE;
    uio.uio_buf = (uint8_t *)"Hello World2";
    uio.uio_offset = 0;
    uio.uio_resid = 13;
    state = vfs_rdwr(dest, &uio, 0);
    kassert(state == 0);

    // read it again
    uio.uio = UIO_READ;
    uio.uio_resid = 13;
    uio.uio_buf = kcalloc(1, 13);
    state = vfs_rdwr(dest, &uio, 0);
    kassert(state == 0);



    file_handle_t hnd;
    vfs_lookup(NULL_HANDLE, "/testdir", &hnd);
    struct vfs_vattr at = {
        .va_type = VFS_VN_DIR
    };
    vfs_create(hnd, "hohoho", 6, &at, &hnd);
    at.va_type = VFS_VN_REG;
    vfs_create(hnd, "file.c", 6, &at, &hnd);
    vfs_lookup(NULL_HANDLE, "/testdir/hohoho/file.c/", &hnd);

    kfree(uio.uio_buf);



    // attempt to overwrite mount point
    struct ramdisk *rd = rd_new_ramdisk();
    struct vfs_fs *ramfs = ramfs_new_fs();

    file_handle_t mountpoint;
    kassert(!vfs_lookup(NULL_HANDLE, "/", &mountpoint));
    state = vfs_mount(ramfs, mountpoint, rd);

    state = vfs_lookup(NULL_HANDLE, "/", &mountpoint);

    attribs.va_type = VFS_VN_REG;
    state = vfs_create(mountpoint, "testdir2", 8, &attribs, &out);

    state = vfs_lookup(NULL_HANDLE, "/testdir2", &mountpoint);

    state = vfs_lookup(NULL_HANDLE, "/testfile", &mountpoint);


    rd = rd_new_ramdisk();
    ramfs = ramfs_new_fs();

    kassert(!vfs_lookup(NULL_HANDLE, "/", &mountpoint));
    state = vfs_mount(ramfs, mountpoint, rd);

    state = vfs_lookup(NULL_HANDLE, "/", &mountpoint);

    attribs.va_type = VFS_VN_REG;
    state = vfs_create(mountpoint, "testdir12", 9, &attribs, &out);

    state = vfs_lookup(NULL_HANDLE, "/testdir12", &mountpoint);

    state = vfs_lookup(NULL_HANDLE, "/testdir1", &mountpoint);
}