#include "interrupt.h"
#include "vfs.h"
#include "rbtree.h"
#include "string.h"
#include "kheap.h"
#include "kprintf.h"

struct pnc_entry pnc_root_dir = {
    .entries_root = RB_NIL,
    .key = 0,
    .name_len = 1,
    .path_section = "/",
    .parent = NULL,
    .next = NULL,
    .prev = NULL
};

struct pnc_entry *new_pnc_entry(struct pnc_entry *parent, const char *pathn, size_t len, size_t hash)
{
    struct pnc_entry *ent = kmalloc(sizeof(struct pnc_entry));
    init_root_node(&ent->entries_root);

    ent->next = NULL;
    ent->prev = NULL;

    ent->key = hash;
    ent->parent = parent;

    ent->name_len = len;
    ent->path_section = kmalloc(len);
    strncpy(ent->path_section, pathn, len);

    return ent;
}

struct pnc_entry *pnc_add_section(struct pnc_entry *dir, const char *section, size_t len)
{
    struct pnc_entry *exists = pnc_lookup_section(dir, section, len);
    if (exists) {
        kprintf("warn: %.*s already exists\n", len, section);
        return exists;
    }

    size_t hash = pathn_hash(section, len);
    struct pnc_entry *new = new_pnc_entry(dir, section, len, hash);

    struct pnc_entry *dup = (struct pnc_entry *)tree_insert(&dir->entries_root, (struct rb_tree_data *)new);
    if (dup) {
        kprintf("duplicate on key %lu with string %.*s\n", hash, len, section);
        // we have a duplicate
        struct pnc_entry *old_next = dup->next;
        new->prev = dup;
        new->next = old_next;
        dup->next = new;
        if (old_next != NULL) {
            old_next->prev = new;
        }
    }

    kprintf("inserted %.*s into %.*s\n", (int)new->name_len, new->path_section, dir->name_len, dir->path_section);
    return new;
}

void pnc_evict_section(struct pnc_entry *entry)
{
    struct pnc_entry *evicted;
    if (entry->next || entry->prev) {
        // if either next or prev set, we know there are duplicates for the given key
        struct pnc_entry *first = (struct pnc_entry *)tree_find(&entry->parent->entries_root, entry->key);

        if (entry->name_len == first->name_len && !strncmp(entry->path_section, first->path_section, entry->name_len)) {
            // first one: go to next element in list
            tree_set_data_at(&entry->parent->entries_root, entry->key, (struct rb_tree_data *)first->next);
            first->next->prev = NULL;
        } else {
retry:
            if (entry->name_len != first->name_len || strncmp(entry->path_section, first->path_section, entry->name_len)) {
                if (first->next) {
                    // not yet found, but still elements left
                    first = first->next;
                    goto retry;
                } else
                    // not found and list empty
                    kpanic(0, NULL, "requested path for eviction not found");
            }
            // not first one in list: unlink it, no need to update tree
            if (first->prev)
                first->prev->next = first->next;
            if (first->next)
                first->next->prev = first->prev;
        }

        first->next = first->prev = (void *)0xDEADBEEF;
        evicted = first;
    } else {
        // no duplicates
        evicted = (struct pnc_entry *)tree_remove(&entry->parent->entries_root, entry->key);
    }

    kprintf("evicted %.*s in %.*s\n", (int)entry->name_len, entry->path_section,
        entry->parent->name_len, entry->parent->path_section);

    kfree(evicted->path_section);
    kfree(evicted);
}

struct pnc_entry *pnc_lookup_section(struct pnc_entry *dir, const char *section, size_t len)
{
    size_t hash = pathn_hash(section, len);

    struct pnc_entry *found = (struct pnc_entry *)tree_find(&dir->entries_root, hash);

    if (!found) 
        return NULL;

    // find fitting string
    while (len != found->name_len || strncmp(section, found->path_section, len)) {
        if (found->next)
            found = found->next;
        else
            return NULL;
    }
    return found;
}

static void _print_tree(rb_tree_node_t *root, size_t depth)
{   
    if (root != RB_NIL) {
        _print_tree(root->child[RB_LEFT], depth);
        
        struct pnc_entry *curr = (struct pnc_entry *)root->data;
        struct pnc_entry *bka = curr;
        while (curr) {
            for (size_t i = 0; i < depth; i++)
                kprintf("    ");

            kprintf("%.*s: %.*s\n", curr->parent->name_len,
                curr->parent->path_section, curr->name_len, curr->path_section);
            curr = curr->next;
        }

        while (bka) {
            pnc_debugprint(bka, depth + 1);
            bka = bka->next;
        }

        _print_tree(root->child[RB_RIGHT], depth);
    }
}

void pnc_debugprint(struct pnc_entry *rootdir, size_t depth)
{
    if (rootdir) {
        _print_tree(rootdir->entries_root, depth);
    }
}