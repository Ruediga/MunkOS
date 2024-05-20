#include "pathn.h"
#include "kprintf.h"
#include "string.h"
#include "macros.h"
#include "vfs.h"

inline int path_is_root(const char *path) {
    // . and .. handled by lookup
    return *path == '/';
}

inline void pathn_buffer(char *buffer, const char *path) {
    strncpy(buffer, path, 255);
    buffer += strlen(path);
    *buffer = '\0';
}

inline void pathn_buffer_n(char *buffer, const char *path, size_t n) {
    strncpy(buffer, path, MIN(n, 255));
    *(buffer + n) = '\0';
}

char *copy_first_section(const char *pathname, char **path_remaining, char *buffer) {
    // skip leading seperators
    while (*pathname == SEPERATOR) {
        pathname++;
    }

    // pos of first seperator after this section
    const char *next_sep = strchr(pathname, SEPERATOR);

    if (next_sep == NULL) {
        *path_remaining = NULL;
        return strcpy(buffer, pathname);
    } else {
        // extract first section
        size_t length = next_sep - pathname;
        if (length > MAX_SECTION_LENGTH - 1)
            length = MAX_SECTION_LENGTH - 1;
        strncpy(buffer, pathname, length);
        buffer[length] = '\0';

        *path_remaining = (char *)(next_sep);

        return buffer;
    }
}

// ignores trailing slashes
const char *split_path(const char *path, size_t *length) {
    if (path == NULL || *path == '\0') {
        *length = 0;
        return NULL;
    }

    const char *start = path;
    while (*start == '/') {
        start++;
    }

    if (*start == '\0')
        return NULL;

    const char *delimiter = strchr(start, '/');
    if (delimiter == NULL) {
        *length = strlen(start);
        return (const char *)start;
    }

    *length = delimiter - start;

    return (const char *)start;
}

static void _pathn_bt_print(file_handle_t hnd, bool first)
{
    if (hnd.cache_entry != root_fs->vfs_root_pnc) {
        while (hnd.cache_entry->vnode->v_flag & VFS_VN_FLAG_ROOT) {
            if (hnd.cache_entry == root_fs->vfs_root_pnc) {
                kprintf("%.*s", hnd.cache_entry->name_len, hnd.cache_entry->path_section);
                return;
            }

            // we are at a root vnode, so jump to the underlying fs
            hnd.cache_entry = hnd.fs->vfs_covered.cache_entry;
            hnd.fs = hnd.cache_entry->vnode->v_vfsp;
        }

        file_handle_t p = {
            .cache_entry = hnd.cache_entry->parent,
            .fs = p.cache_entry->vnode->v_vfsp
        };
        _pathn_bt_print(p, false);
        kprintf("%.*s", (int)hnd.cache_entry->name_len, hnd.cache_entry->path_section);

        if (!first)
            kprintf("/");
        else
            kprintf(hnd.cache_entry->vnode->v_type == VFS_VN_DIR ? "/" : "");
    } else
        kprintf("%.*s", (int)hnd.cache_entry->name_len, hnd.cache_entry->path_section);
}

void pathn_bt_print(file_handle_t hnd)
{
    _pathn_bt_print(hnd, true);
}