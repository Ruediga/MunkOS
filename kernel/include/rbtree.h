#pragma once

#include <stddef.h>
#include <stdbool.h>

enum rb_tree_color {
    RB_RED,
    RB_BLACK
};

enum rb_tree_direction {
    RB_LEFT,
    RB_RIGHT
};

struct rb_tree_data {
    size_t key;
    // you'd define more data that in memory, lies right after the key.
};

typedef struct rb_tree_node {
    struct rb_tree_node *parent;
    struct rb_tree_node *child[2];
    enum rb_tree_color color;
    struct rb_tree_data *data;
} rb_tree_node_t;

// leaves
#define RB_NIL &sentinel
extern rb_tree_node_t sentinel;

// init a root node pointer.
static inline void init_root_node(rb_tree_node_t **root) {
    *root = RB_NIL;
};
// insert an element into the tree. returns 1 upon failure (duplicate found)
struct rb_tree_data *tree_insert(rb_tree_node_t **root, struct rb_tree_data *data);
// find the element associated with the given key, and remove it from the tree.
// return the removed element, else NULL
struct rb_tree_data *tree_remove(rb_tree_node_t **root, size_t key);
// return the element associated with the given tree. return the found element,
// else NULL.
struct rb_tree_data *tree_find(rb_tree_node_t **root, size_t key);
// changes out the data pointer at key. key has to be the same as before.
// use only for managing buckets.
void tree_set_data_at(rb_tree_node_t **root, size_t key, struct rb_tree_data *data);

void tree_debug_print(rb_tree_node_t *root, size_t depth);